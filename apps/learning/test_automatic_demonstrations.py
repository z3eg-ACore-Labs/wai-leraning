"""Synthetic lifecycle and publication checks; no Windows hooks or production data."""
import json
import argparse
import threading
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from automatic_demonstrations import Recorder
from client_training import initialize, maintain
from client_tracks import read_tracks
from client_input import WindowsInput
from automatic_demonstrations import run
from dungeon_runs import policy_home


def packet(seq=1, run='123', instance=7, state='active', completed=False, interrupted=False, bosses=0, **obs_changes):
    obs = dict(schema=1, backend='client_input_probe', generation=run, seq=seq,
               player_guid='42', map=389, instance=instance, supported=True, ready=True,
               features=[.1]*16, boss_progress_available=True, bosses=bosses)
    obs.update(obs_changes)
    return dict(schema=1, backend='human_dungeon_watch', state=state, seq=seq,
                player_guid='42', map=389, instance=instance, run=run,
                completed=completed, interrupted=interrupted, bosses=bosses, observation=obs)


class AutomaticTests(unittest.TestCase):
    @patch('client_training_history.record_client_training', return_value=True)
    def test_generic_dungeon_completion_isolated_store_and_missing_coverage(self, history):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); submitted=[]
            recorder=Recorder(root/'logs','42',26,submitted.append)
            def generic(seq, **changes):
                row=packet(seq);row.update(map=33,difficulty=1,completion_available=True,
                    completion_source='unavailable',required_mask=7,completed_mask=0)
                row['observation'].update(map=33,boss_progress_available=False)
                row.update(changes);return row
            features=recorder.observe(generic(1))
            recorder.sample(features, {'forward'}, True, True)
            with self.assertRaises(ValueError):
                recorder.observe(generic(2,completed=True,completion_source='encounter_mask',completed_mask=4))
            recorder.observe(generic(3,state='completion_pending',completed=True,completion_source='encounter_mask',completed_mask=7))
            self.assertEqual(len(submitted),1)
            initialize(root/'base',inputs=26)
            with self.assertRaises(ValueError): maintain(root/'base',submitted[0])
            home=policy_home(root/'base',generic(4))
            initialize(home,inputs=26,dungeon_map=33,difficulty=1)
            report=maintain(home,submitted[0])
            self.assertEqual(report['hybrid']['reinforcement_decisions'],0)
            self.assertEqual(report['rl']['status'],'explicit_reinforcement_only')
            recorder=Recorder(root/'unknown','42',26,submitted.append)
            recorder.observe(generic(1,completion_available=False,required_mask=0))
            with self.assertRaises(ValueError):
                recorder.observe(generic(2,completed=True,completion_source='unavailable'))
            recorder.end('incomplete',reason='unsupported_completion')

    def test_passive_input_cannot_generate_keys(self):
        native = object.__new__(WindowsInput)
        native.read_only = True
        with self.assertRaises(RuntimeError): native.key(87, True)

    @patch('client_training_history.record_client_training', return_value=True)
    def test_resident_observer_only_polls_and_stops_with_owner(self, history):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); home = root/'store'; initialize(home, inputs=16)
            bindings = root/'bindings.json'
            bindings.write_text(json.dumps(dict(forward=87, back=83, left=81, right=69)))
            commands = []
            class Soap:
                def command(self, text):
                    commands.append(text)
                    return json.dumps(packet(len(commands), completed=len(commands)==2, bosses=4 if len(commands)==2 else 0))
            class Kernel:
                closed = False
                def WaitForSingleObject(self, *args): return 0x102 if len(commands)<2 else 0
                def CloseHandle(self, *args): self.closed = True
            class Native:
                pid=123; input_revision=0; injected_until=0; recording_paused=False; error=None
                stop_requested=threading.Event()
                closed=False
                def focused(self): return True
                def take_controls(self): return set()
                def close(self): self.closed=True
            class Keyboard:
                def __init__(self, keys): pass
                def movement(self): return {'forward'}
                def interfering(self): return False
            kernel, native, submitted = Kernel(), Native(), []
            args = argparse.Namespace(player_guid='42', pulse=.1, learning_home=home, bindings=bindings,
                                      owner_pid=99, pid=None, log_dir=root/'logs')
            with patch('automatic_demonstrations.make_soap', return_value=Soap()), \
                 patch('automatic_demonstrations.owner_handle', return_value=(kernel,123)), \
                 patch('automatic_demonstrations.wow_processes', return_value=[123]), \
                 patch('automatic_demonstrations.WindowsInput', return_value=native) as factory, \
                 patch('automatic_demonstrations.Keyboard', Keyboard), \
                 patch('automatic_demonstrations.maintain', side_effect=lambda home,path: submitted.append(path)):
                run(args)
            factory.assert_called_once_with(123, controls_only=True, read_only=True)
            self.assertTrue(kernel.closed and native.closed)
            self.assertEqual(len(submitted), 1)
            self.assertTrue(all(command.startswith('wai client watch 42 ') for command in commands))

    def test_entry_death_exit_reentry_completion_and_duplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            submitted = []
            recorder = Recorder(directory, '42', 16, submitted.append)
            features = recorder.observe(packet())
            recorder.sample(features, {'forward'}, True, True)
            recorder.observe(packet(2, ready=False, outcome='player_died'))
            self.assertIsNotNone(recorder.stream)  # Wipe is not the dungeon terminal.
            self.assertIsNone(recorder.observe(packet(3, state='outside_or_ineligible')))
            self.assertEqual(submitted, [])
            features = recorder.observe(packet(4))
            recorder.sample(features, {'left'}, True, True)
            recorder.observe(packet(5, completed=True, bosses=4))
            self.assertEqual(len(submitted), 1)
            records = [json.loads(line) for line in submitted[0].read_text().splitlines()]
            self.assertEqual(records[-1]['samples'], 2)
            self.assertEqual(records[-1]['outcome'], 'completed_all_bosses')
            recorder.observe(packet(6, completed=True, bosses=4))
            restarted = Recorder(directory, '42', 16, submitted.append)
            restarted.observe(packet(1, completed=True, bosses=4))
            self.assertEqual(len(submitted), 1)

    def test_abandon_disconnect_conflict_and_restart_cannot_train(self):
        for state in ('offline', 'blocked', 'active'):
            with self.subTest(state=state), tempfile.TemporaryDirectory() as directory:
                submitted = []
                recorder = Recorder(directory, '42', 26, submitted.append)
                self.assertEqual(len(recorder.observe(packet())), 26)
                recorder.observe(packet(2, state=state, interrupted=state=='active'))
                recorder.observe(packet(3, completed=True, bosses=4))
                self.assertEqual(submitted, [])
                self.assertIn('incomplete', next(Path(directory).glob('*.jsonl')).read_text())
        with tempfile.TemporaryDirectory() as directory:
            submitted = []
            recorder = Recorder(directory, '42', 16, submitted.append)
            recorder.observe(packet()); recorder.end('incomplete', reason='stop')
            restarted = Recorder(directory, '42', 16, submitted.append)
            self.assertIsNone(restarted.observe(packet()))
            restarted.observe(packet(2, completed=True, bosses=4))
            self.assertEqual(submitted, [])

    def test_instance_switch_and_wrong_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            submitted = []
            recorder = Recorder(directory, '42', 16, submitted.append)
            recorder.observe(packet())
            recorder.observe(packet(2, run='456', instance=8))
            recorder.observe(packet(3, run='456', instance=8, completed=True, bosses=4))
            self.assertEqual(len(submitted), 1)
            bad = packet(4); bad['player_guid'] = '99'
            with self.assertRaises(ValueError): recorder.observe(bad)
            with self.assertRaises(ValueError): recorder.observe(packet(3))

    def test_invalid_samples_and_inconsistent_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            recorder = Recorder(directory, '42', 16, lambda path: self.fail('must not complete'))
            features = recorder.observe(packet())
            for keys, stable, ready in [({'forward'},False,True), ({'left'},True,False),
                                         ({'forward','back'},True,True), (set(),True,True)]:
                recorder.sample(features, keys, stable, ready)
            self.assertEqual(recorder.samples, 0)
            with self.assertRaises(ValueError): recorder.observe(packet(2, completed=True, bosses=3))
            recorder.end('incomplete')

    @patch('client_training_history.record_client_training', return_value=True)
    def test_completed_human_run_updates_only_imitation_and_hybrid(self, history):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); home = root/'store'; initialize(home, inputs=16)
            old = read_tracks(home/'models.json'); old_rl = old['rl'].read_bytes()
            submitted = []
            recorder = Recorder(root/'logs', '42', 16, submitted.append)
            features = recorder.observe(packet())
            for _ in range(32): recorder.sample(features, {'forward'}, True, True)
            recorder.observe(packet(2, completed=True, bosses=4))
            report = maintain(home, submitted[0])
            new = read_tracks(home/'models.json')
            self.assertEqual(new['rl'], old['rl']); self.assertEqual(new['rl'].read_bytes(), old_rl)
            self.assertEqual(report['rl']['status'], 'explicit_reinforcement_only')
            for track in ('il','hybrid'):
                self.assertEqual(report[track]['status'], 'updated')
                self.assertEqual(report[track]['reinforcement_decisions'], 0)
            registry = (home/'models.json').read_bytes()
            maintain(home, submitted[0])
            self.assertEqual(registry, (home/'models.json').read_bytes())
            rows = [json.loads(x) for x in submitted[0].read_text().splitlines()]
            rows.insert(1, {'event':'reinforcement_transition','track':'rl'})
            submitted[0].write_text('\n'.join(map(json.dumps, rows)))
            with self.assertRaises(ValueError): maintain(home, submitted[0])


if __name__ == '__main__': unittest.main()
