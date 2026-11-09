"""Offline tests only: no native hooks, SOAP calls, client input or live model changes."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

import torch

from client_demonstrations import CONTRACT, fit, read_samples
from client_input import GuardedInput
from client_probe import FrozenNavigation, load_frozen
from client_session import Modes, movement_action, run


class Native:
    def __init__(self, *_, **kwargs):
        self.focus = True
        self.error = None
        self.stop_requested = threading.Event()
        self.events = []
        self.guard = GuardedInput(self)
    def focused(self): return self.focus
    def key(self, key, down): self.events.append((key,down))
    def close(self): self.guard.stop('closed')
    def take_controls(self):return set()


class ClientSessionTests(unittest.TestCase):
    def test_mode_switch_releases_and_requires_explicit_ready_focus(self):
        native = Native()
        modes = Modes(native)
        self.assertEqual(modes.mode, 'paused')
        self.assertEqual(modes.switch('model', False), 'paused')
        modes.switch('model', True)
        native.guard.pulse([87], .5)
        modes.switch('demonstrate')
        self.assertEqual(native.events, [(87,True),(87,False)])
        self.assertFalse(native.guard.armed)
        native.focus = False
        self.assertEqual(modes.switch('model', True), 'paused')
        native.focus = True
        self.assertEqual(modes.switch('model', True), 'model')
        native.guard.stop('manual_keyboard_input')
        native.guard.arm()
        self.assertFalse(native.guard.armed)
        self.assertEqual(modes.switch('model', True), 'model')

    def test_only_unambiguous_movement_labels(self):
        self.assertIsNone(movement_action([]))
        self.assertIsNone(movement_action(['forward','back']))
        self.assertEqual(movement_action(['forward','left']),2)
        self.assertEqual(movement_action(['right']),7)

    def prepare(self, root):
        checkpoint, log = root/'source.pt', root/'demo.jsonl'
        torch.manual_seed(7)
        torch.save({'schema':1,'algorithm':'ppo','architecture':[16,64,64,9],
                    'version':6,'model':FrozenNavigation(16).state_dict()}, checkpoint)
        records = [{'event':'session_start','action_contract':CONTRACT}]
        records += [{'event':'demonstration','features':[.1]*16,'action':1,
                     'source':'human','stable':True} for _ in range(40)]
        records += [{'event':'policy_decision','features':[.2]*16,'action':7,'source':'model'}]
        log.write_text('\n'.join(map(json.dumps,records))+'\n')
        return checkpoint, log

    def test_fit_preserves_source_critic_and_excludes_model_actions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, log = self.prepare(root)
            digest = hashlib.sha256(source.read_bytes()).digest()
            original = torch.load(source,weights_only=True)
            result = fit(source,log,root/'new.pt')
            saved = torch.load(root/'new.pt',weights_only=True)
            self.assertEqual(result['samples'],40)
            self.assertLess(result['loss_after'], result['loss_before'])
            self.assertEqual(digest,hashlib.sha256(source.read_bytes()).digest())
            for key in ('critic.weight','critic.bias'):
                self.assertTrue(torch.equal(original['model'][key],saved['model'][key]))
            self.assertEqual(saved['algorithm'],'client_bc')
            load_frozen(root/'new.pt')
            with self.assertRaises(ValueError): fit(source,log,source)
            with self.assertRaises(ValueError): fit(source,log,root/'new.pt')

    def test_bad_or_insufficient_samples_do_not_publish(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            source, log=self.prepare(root)
            lines=log.read_text().splitlines()
            log.write_text('\n'.join(lines[:3]))
            with self.assertRaises(ValueError):fit(source,log,root/'new.pt')
            self.assertFalse((root/'new.pt').exists())
            source,log=self.prepare(root)
            records=[json.loads(line) for line in log.read_text().splitlines()]
            records[1]['source']='model'
            log.write_text('\n'.join(map(json.dumps,records)))
            with self.assertRaises(ValueError):read_samples(log,16)

    def exercise_session(self, mode='demonstrate', fail=False,learn=False):
        class Clock:
            value=0
            def now(self):return self.value
            def sleep(self,n):self.value+=n
        class Soap:
            def __init__(self):self.calls=[];self.seq=0
            def command(self,command):
                self.calls.append(command)
                self.seq+=1
                if fail and self.seq==2: raise TimeoutError('test transport failure')
                return json.dumps({'schema':1,'backend':'client_input_probe','features':[0]*16,
                    'seq':self.seq,'generation':'123','player_guid':'42','map':389,'instance':7,
                    'ready':True,'supported':True,'boss_progress_available':True,'episode_time_available':True,
                    'bosses':0,'deaths':0,'elapsed_ms':self.seq*100,'outcome':'running'})
        class Keyboard:
            def __init__(self,*_):pass
            def down(self,key):return key==({'demonstrate':0x75,'model':0x78}[mode]) and clock.value==0
            def movement(self):return frozenset(['forward'])
            def interfering(self):return False
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source,_=self.prepare(root)
            bindings=root/'keys.json'
            bindings.write_text(json.dumps({'forward':87,'back':83,'left':81,'right':69}))
            args=argparse.Namespace(checkpoint=source,bindings=bindings,pid=10,player_guid='42',
                log=root/'session.jsonl',seconds=1,pulse=.1,seed=1,train_output=None,learn=learn)
            if learn:
                from client_training import initialize
                args.learning_home=root/'store'
                initialize(args.learning_home,inputs=16)
            clock,soap,native=Clock(),Soap(),Native()
            with patch('client_session.WindowsInput',return_value=native),patch('client_session.Keyboard',Keyboard),\
                 patch('client_session.make_soap',return_value=soap),\
                 patch('client_session.time.monotonic',clock.now),patch('client_session.time.sleep',clock.sleep),\
                 patch('client_training_history.record_client_training',return_value=True):
                if fail:
                    with self.assertRaises(TimeoutError):run(args)
                else:
                    def forced(model,features,*_):
                        return 1,torch.softmax(model(torch.tensor(features,dtype=torch.float32)),dim=-1).tolist()
                    with patch('client_session.choose',side_effect=forced):run(args)
            records=[json.loads(line) for line in args.log.read_text().splitlines()]
            self.assertEqual(soap.calls[-1],'wai client stop 123')
            self.assertTrue(all(c=='wai client observe' for c in soap.calls[:-1]))
            self.assertFalse(native.guard.held)
            if learn:
                report=args.learning_home/'latest-training-summary.json'
                native.training_report=json.loads(report.read_text()) if report.exists() else None
            return records,native

    def test_recording_session_never_sends_input_and_releases_scoped_lease(self):
        records,native=self.exercise_session()
        self.assertTrue(any(r['event']=='demonstration' for r in records))
        self.assertFalse(any(r['event']=='policy_decision' for r in records))
        self.assertEqual(native.events,[])

    def test_model_session_does_not_generate_human_labels(self):
        records,native=self.exercise_session('model')
        self.assertTrue(any(r['event']=='policy_decision' for r in records))
        self.assertFalse(any(r['event']=='demonstration' for r in records))
        self.assertTrue(native.events)
        self.assertEqual(native.events[-1],(87,False))

    def test_transport_failure_cleans_up_model_input(self):
        records,native=self.exercise_session('model',fail=True)
        self.assertEqual(records[-1]['status'],'failed_or_interrupted')
        self.assertFalse(native.guard.armed)

    def test_session_automatically_updates_only_its_reward_policy_after_release(self):
        records,native=self.exercise_session('model',learn=True)
        self.assertTrue(any(r['event']=='reinforcement_transition' for r in records))
        self.assertEqual(native.training_report['tracks']['rl']['status'],'updated')
        self.assertEqual(native.training_report['tracks']['hybrid']['status'],'no_eligible_samples')
        self.assertEqual(native.training_report['tracks']['il']['status'],'no_eligible_samples')

    def test_failed_session_never_automatically_updates_weights(self):
        _,native=self.exercise_session('model',fail=True,learn=True)
        self.assertIsNone(native.training_report)


if __name__=='__main__':unittest.main()
