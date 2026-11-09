"""Parallel collection, shared PPO update and failure handling without a running server."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Barrier
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import torch

from parallel_loop import PartySoap, merge_counts, run_parallel
from rl_loop import advantages_for, NavigationPolicy, checkpoint
from test_rl_loop import EpisodeTransport


class PoolTransport:
    def __init__(self, outcomes=('wipe', 'completed_all_bosses')):
        self.parties = [EpisodeTransport(outcome) for outcome in outcomes]
        self.commands = []
        self.barrier = Barrier(len(self.parties))
        self.stopped = False

    def command(self, text):
        self.commands.append(text)
        if text == 'waiepisode stop':
            self.stopped = True
            for party in self.parties:
                party.finished = True
                party.outcome = 'stopped_by_admin'
            return 'queued'
        _, _, number, command = text.split(' ', 3)
        slot = int(number)
        party = self.parties[slot - 1]
        if command.startswith('act '):
            # If collection were sequential the first party would time out here.
            self.barrier.wait(timeout=5)
        response = party.command('waiepisode ' + command)
        if command == 'observe':
            value = json.loads(response)
            value['episode'] = f'{slot * 1000 + party.index}-1'
            return json.dumps(value)
        return response


class ParallelTests(unittest.TestCase):
    def test_escape_and_success_share_one_update_and_both_get_summaries(self):
        transport = PoolTransport(('wandering_off_limits', 'completed_all_bosses'))
        with TemporaryDirectory() as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=11, episodes=1, parties=2, url='http://127.0.0.1:7878/')
            with patch('parallel_loop.Soap', return_value=transport), patch('parallel_loop.time.sleep'), \
                    patch('rl_loop.time.sleep'):
                run_parallel(args)
            self.assertEqual(torch.load(home / 'navigation.pt', weights_only=True)['version'], 1)
            for slot in (1, 2):
                self.assertTrue(any(c.startswith(f'waiepisode party {slot} training report ')
                                    for c in transport.commands))

    def setUp(self):
        history = patch('parallel_loop.record_training')
        self.history = history.start()
        self.addCleanup(history.stop)

    def test_terminal_advantages_do_not_leak_between_parties(self):
        result = advantages_for([dict(value=0, reward=1, done=True),
                                 dict(value=0, reward=100, done=True)])
        self.assertEqual(result.tolist(), [1, 100])

    def test_shared_visit_counts_merge_deltas_not_snapshots(self):
        self.assertEqual(merge_counts({'a': 5}, [{'a': 7, 'b': 1}, {'a': 6, 'b': 2}]),
                         {'a': 8, 'b': 3})

    def test_party_namespace_cannot_rewrite_other_commands(self):
        with self.assertRaises(ValueError):
            PartySoap(None, 0)
        with self.assertRaises(ValueError):
            PartySoap(None, 1).command('account delete someone')

    def test_two_concurrent_parties_share_checkpoint_and_repeat_after_update(self):
        transport = PoolTransport()
        with TemporaryDirectory() as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=11, episodes=2, parties=2, url='http://127.0.0.1:7878/')
            with patch('parallel_loop.Soap', return_value=transport), patch('parallel_loop.time.sleep'), \
                    patch('rl_loop.time.sleep'):
                run_parallel(args)
            saved = torch.load(home / 'navigation.pt', weights_only=True)
            self.assertEqual(saved['version'], 2)  # Two shared updates, not four competing updates.
            self.assertEqual(saved['exploration_counts']['0,0,0'], 4)
            for version in (1, 2):
                manifest = json.loads((home / 'batches' / f'v{version}' / 'complete.json').read_text())
                self.assertEqual(len(manifest['parties']), 2)
                self.assertEqual({p['policy_version'] for p in manifest['parties']}, {version - 1})
            for episode in ('1001-1', '1002-1', '2001-1', '2002-1'):
                self.assertTrue((home / 'episodes' / episode / 'trained.json').exists())
            for slot in (1, 2):
                training = next(i for i, command in enumerate(transport.commands)
                                if command.startswith(f'waiepisode party {slot} training completed'))
                repeat = transport.commands.index(f'waiepisode party {slot} repeat')
                self.assertLess(training, repeat)
            self.assertFalse(transport.stopped)
            self.assertEqual(self.history.call_count, 2)

    def test_invalid_environment_stops_all_parties_without_training(self):
        transport = PoolTransport(('wipe', 'setup_timeout'))
        with TemporaryDirectory() as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=11, episodes=1, parties=2, url='http://127.0.0.1:7878/')
            with patch('parallel_loop.Soap', return_value=transport), patch('parallel_loop.time.sleep'), \
                    patch('rl_loop.time.sleep'):
                with self.assertRaisesRegex(RuntimeError, 'quarantined'):
                    run_parallel(args)
            self.assertEqual(torch.load(home / 'navigation.pt', weights_only=True)['version'], 0)
            self.assertTrue(transport.stopped)
            self.assertFalse(any(' repeat' in command for command in transport.commands))

    def test_four_parties_resume_existing_model_with_one_update(self):
        transport = PoolTransport(('wipe',) * 4)
        with TemporaryDirectory() as temporary:
            home = Path(temporary)
            model = NavigationPolicy()
            optimizer = torch.optim.Adam(model.parameters(), lr=3e-4)
            checkpoint(home / 'navigation.pt', model, optimizer, 5, 'previous-run', exploration_counts={'0,0,0': 10})
            args = SimpleNamespace(home=home, seed=11, episodes=1, parties=4, url='http://127.0.0.1:7878/')
            with patch('parallel_loop.Soap', return_value=transport), patch('parallel_loop.time.sleep'), \
                    patch('rl_loop.time.sleep'):
                run_parallel(args)
            saved = torch.load(home / 'navigation.pt', weights_only=True)
            self.assertEqual(saved['version'], 6)
            self.assertEqual(saved['exploration_counts']['0,0,0'], 14)
            self.assertEqual(sum('training completed' in command for command in transport.commands), 4)

    def test_failed_batch_update_keeps_previous_checkpoint(self):
        transport = PoolTransport()
        with TemporaryDirectory() as temporary:
            home = Path(temporary)
            args = SimpleNamespace(home=home, seed=11, episodes=1, parties=2, url='http://127.0.0.1:7878/')
            with patch('parallel_loop.Soap', return_value=transport), patch('parallel_loop.time.sleep'), \
                    patch('rl_loop.time.sleep'), patch('parallel_loop.update', side_effect=ValueError('fixture failure')):
                with self.assertRaisesRegex(ValueError, 'fixture failure'):
                    run_parallel(args)
            self.assertEqual(torch.load(home / 'navigation.pt', weights_only=True)['version'], 0)
            self.assertTrue(transport.stopped)
            self.assertTrue(any('training failed' in command for command in transport.commands))


if __name__ == '__main__':
    unittest.main()
