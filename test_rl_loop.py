"""Offline RL regression tests; no game server or database is contacted."""
import unittest
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

import torch

from rl_loop import NavigationPolicy, reward, update, run, checkpoint, rollout
from exploration import INPUTS


class RewardTests(unittest.TestCase):
    def test_off_limits_has_terminal_penalty_without_erasing_progress_rewards(self):
        before = dict(bosses=0, deaths=0, elapsed_ms=0)
        after = dict(bosses=1, deaths=0, elapsed_ms=1000, phase='finished', outcome='wandering_off_limits')
        self.assertAlmostEqual(reward(before, after), -90.01)
        self.assertLess(reward(before, after), reward(before, dict(after, outcome='timeout')))

    def test_success_dominates_early_failure(self):
        initial = dict(bosses=0, deaths=0, elapsed_ms=0)
        success = dict(bosses=4, deaths=0, elapsed_ms=3600000,
                       phase='finished', outcome='completed_all_bosses')
        failure = dict(bosses=3, deaths=0, elapsed_ms=1000, phase='finished', outcome='wipe')
        self.assertGreater(reward(initial, success), reward(initial, failure))

    def test_deaths_and_time_have_independent_costs(self):
        initial = dict(bosses=0, deaths=0, elapsed_ms=0)
        alive = dict(bosses=1, deaths=0, elapsed_ms=1000, phase='running')
        dead = dict(alive, deaths=1)
        slow = dict(alive, elapsed_ms=11000)
        self.assertAlmostEqual(reward(initial, alive) - reward(initial, dead), 5)
        self.assertGreater(reward(initial, alive), reward(initial, slow))


class PolicyTests(unittest.TestCase):
    def test_mask_prevents_movement_while_busy(self):
        policy = NavigationPolicy()
        distribution, _ = policy(torch.zeros(INPUTS), torch.tensor([True] + [False] * 8))
        self.assertEqual(distribution.probs[0].item(), 1)
        self.assertEqual(distribution.probs[1:].sum().item(), 0)

    def test_update_produces_measured_same_state_summary(self):
        torch.manual_seed(7)
        policy = NavigationPolicy()
        optimizer = torch.optim.Adam(policy.parameters(), lr=3e-4)
        transitions = []
        for i in range(4):
            state = [0.1] * INPUTS
            state[5], state[7] = 0.75, 0
            mask = [True] * 9
            with torch.no_grad():
                distribution, value = policy(torch.tensor(state), torch.tensor(mask))
            transitions.append(dict(state=state, mask=mask, action=i,
                                    log_probability=distribution.log_prob(torch.tensor(i)).item(),
                                    value=value.item(), reward=float(i)))
        metrics, summary = update(policy, optimizer, transitions)
        self.assertGreater(summary['weight_delta'], 0)
        self.assertEqual(summary['samples'], 4)
        self.assertAlmostEqual(summary['health'], 0.75)
        self.assertTrue(all(torch.isfinite(torch.tensor(x)) for x in metrics.values()))
        self.assertTrue(0 <= summary['probability_before'] <= 1)
        self.assertTrue(0 <= summary['probability_after'] <= 1)


class EpisodeTransport:
    """In-memory episode execution; never starts or contacts a game server."""
    def __init__(self, outcome='completed_all_bosses'):
        self.commands = []
        self.outcome = outcome
        self.index = 1
        self.finished = False

    def command(self, text):
        self.commands.append(text)
        if text == 'waiepisode observe':
            return json.dumps(dict(schema=1, episode=f'123-{self.index}', mode='cloned',
                phase='finished' if self.finished else 'running', seq=2 if self.finished else 1,
                features=[0.0] * 16, elapsed_ms=1000 if self.finished else 0,
                bosses=(4 if self.outcome == 'completed_all_bosses' else 1) if self.finished else 0,
                deaths=5 if self.finished and self.outcome == 'wipe' else 0,
                outcome=self.outcome if self.finished else '', auto_repeat=True,
                action_result='wait'))
        if text.startswith('waiepisode act '):
            self.finished = True
            return 'WAI_ACTION_QUEUED'
        if text.startswith('waiepisode training report '):
            return 'WAI_TRAINING_REPORT_RECORDED'
        if text.startswith('waiepisode training '):
            return 'WAI_TRAINING_RECORDED'
        if text == 'waiepisode repeat':
            self.index += 1
            self.finished = False
            return 'queued'
        raise AssertionError(text)


class LoopTests(unittest.TestCase):
    def setUp(self):
        history = patch('rl_loop.record_training')
        self.history = history.start()
        self.addCleanup(history.stop)

    def test_off_limits_trains_before_restarting_and_preserves_checkpoint(self):
        transport = EpisodeTransport('wandering_off_limits')
        with TemporaryDirectory(dir=Path(__file__).resolve().parent.parent / 'output') as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=7, episodes=2, url='http://127.0.0.1:7878/')
            with patch('rl_loop.Soap', return_value=transport), patch('rl_loop.time.sleep'):
                run(args)
            self.assertEqual(torch.load(home / 'navigation.pt', weights_only=True)['version'], 2)
            trajectory = json.loads((home / 'episodes' / '123-1' / 'trajectory.json').read_text())
            self.assertEqual(trajectory['terminal']['outcome'], 'wandering_off_limits')
            self.assertEqual(trajectory['transitions'][-1]['exploration_bonus'], 0)
            self.assertLess(trajectory['transitions'][-1]['reward'], -80)
            completed = next(i for i, c in enumerate(transport.commands)
                             if c.startswith('waiepisode training completed '))
            self.assertLess(completed, transport.commands.index('waiepisode repeat'))

    def test_legacy_checkpoint_preserves_predictions_and_saves_backup(self):
        with TemporaryDirectory(dir=Path(__file__).resolve().parent.parent / 'output') as temporary:
            home = Path(temporary)
            policy = NavigationPolicy()
            optimizer = torch.optim.Adam(policy.parameters(), lr=3e-4)
            path = home / 'navigation.pt'
            checkpoint(path, policy, optimizer, 7, '')
            legacy = torch.load(path, weights_only=True)
            legacy['architecture'] = [16, 64, 64, 9]
            legacy['model']['encoder.0.weight'] = legacy['model']['encoder.0.weight'][:, :16].clone()
            with path.open('wb') as stream:
                torch.save(legacy, stream)
            original_bytes = path.read_bytes()
            raw = torch.arange(16, dtype=torch.float32) / 16
            mask = torch.tensor([True] * 9)
            old_policy = NavigationPolicy()
            old_policy.encoder[0] = torch.nn.Linear(16, 64)
            old_policy.load_state_dict(legacy['model'])
            expected, expected_value = old_policy(raw, mask)

            def inspect_then_rollout(soap, model, version, first, counts):
                augmented = torch.cat((raw, torch.ones(INPUTS - 16)))
                actual, actual_value = model(augmented, mask)
                torch.testing.assert_close(actual.probs, expected.probs)
                torch.testing.assert_close(actual_value, expected_value)
                return rollout(soap, model, version, first, counts)

            args = SimpleNamespace(home=home, seed=7, episodes=1, url='http://127.0.0.1:7878/')
            with patch('rl_loop.Soap', return_value=EpisodeTransport()), patch('rl_loop.time.sleep'), \
                    patch('rl_loop.rollout', side_effect=inspect_then_rollout):
                run(args)
            self.assertEqual((home / 'navigation.before-exploration.pt').read_bytes(), original_bytes)
            saved = torch.load(path, weights_only=True)
            self.assertEqual(saved['architecture'][0], INPUTS)
            self.assertEqual(saved['version'], 8)
            self.assertTrue(saved['exploration_counts'])

    def test_wipe_trains_then_starts_fresh_episode(self):
        transport = EpisodeTransport('wipe')
        with TemporaryDirectory(dir=Path(__file__).resolve().parent.parent / 'output') as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=7, episodes=2, url='http://127.0.0.1:7878/')
            with patch('rl_loop.Soap', return_value=transport), patch('rl_loop.time.sleep'):
                run(args)
            saved = torch.load(home / 'navigation.pt', weights_only=True)
            self.assertEqual(saved['version'], 2)
            trajectory = json.loads((home / 'episodes' / '123-1' / 'trajectory.json').read_text())
            self.assertEqual(trajectory['terminal']['outcome'], 'wipe')
            self.assertLess(trajectory['transitions'][-1]['reward'], 0)
            completed = next(i for i, command in enumerate(transport.commands)
                             if command.startswith('waiepisode training completed '))
            self.assertLess(completed, transport.commands.index('waiepisode repeat'))

    def test_completed_run_trains_reports_then_repeats(self):
        transport = EpisodeTransport()
        with TemporaryDirectory(dir=Path(__file__).resolve().parent.parent / 'output') as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=7, episodes=2, url='http://127.0.0.1:7878/')
            with patch('rl_loop.Soap', return_value=transport), patch('rl_loop.time.sleep'):
                run(args)
            saved = torch.load(home / 'navigation.pt', weights_only=True)
            self.assertEqual(saved['version'], 2)
            self.assertEqual(saved['last_episode'], '123-2')
            for episode in ('123-1', '123-2'):
                directory = home / 'episodes' / episode
                self.assertTrue((directory / 'complete.json').exists())
                self.assertTrue((directory / 'trained.json').exists())
                self.assertTrue((directory / 'summary.json').exists())
            commands = transport.commands
            report = next(i for i, c in enumerate(commands) if c.startswith('waiepisode training report'))
            self.assertLess(report, commands.index('waiepisode repeat'))
            self.assertEqual(sum(c.startswith('waiepisode act ') for c in commands), 2)
            self.assertEqual(self.history.call_count, 2)

    def test_failed_training_preserves_previous_checkpoint_and_stops_repeat(self):
        transport = EpisodeTransport()
        with TemporaryDirectory(dir=Path(__file__).resolve().parent.parent / 'output') as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=7, episodes=1, url='http://127.0.0.1:7878/')
            with patch('rl_loop.Soap', return_value=transport), patch('rl_loop.time.sleep'), \
                    patch('rl_loop.update', side_effect=ValueError('fixture failure')):
                with self.assertRaisesRegex(ValueError, 'fixture failure'):
                    run(args)
            self.assertEqual(torch.load(home / 'navigation.pt', weights_only=True)['version'], 0)
            self.assertFalse((home / 'episodes' / '123-1' / 'trained.json').exists())
            self.assertTrue(any(c.startswith('waiepisode training failed ') for c in transport.commands))
            self.assertNotIn('waiepisode repeat', transport.commands)


if __name__ == '__main__':
    unittest.main()
