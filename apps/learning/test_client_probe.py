"""Offline input/transport/checkpoint tests. No native hooks, client, SOAP or server is started."""
import argparse
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import torch

from client_input import GuardedInput
from client_probe import FrozenNavigation, load_frozen, run, read_bindings, validate_observation


class FakeInput:
    def __init__(self):
        self.focus = True
        self.events = []
        self.fail_down = False
    def focused(self):
        return self.focus
    def key(self, key, down):
        self.events.append((key, down))
        if self.fail_down and down:
            raise RuntimeError('synthetic input failure')


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeInput()
        self.now = 0.0
        self.guard = GuardedInput(self.api, lambda:self.now)
        self.guard.arm_allowed = True
        self.guard.arm()

    def test_deadline_releases_even_without_controller_network_progress(self):
        self.guard.pulse([87,81], .5)
        self.now = .51
        self.guard.tick()
        self.assertEqual(self.api.events, [(87,True),(81,True),(81,False),(87,False)]
                         if list({87,81}) == [81,87] else [(87,True),(81,True),(87,False),(81,False)])
        self.assertFalse(self.guard.held)

    def test_focus_loss_revokes_and_releases(self):
        self.guard.pulse([87], .5)
        self.api.focus = False
        self.guard.tick()
        self.assertEqual(self.guard.reason,'focus_lost')
        self.assertEqual(self.api.events[-1],(87,False))
        with self.assertRaises(RuntimeError): self.guard.pulse([87],.5)

    def test_manual_takeover_is_terminal(self):
        self.guard.pulse([87], .5)
        self.guard.stop('manual_keyboard_input')
        self.guard.arm()
        self.assertFalse(self.guard.armed)
        self.assertEqual(self.api.events[-1],(87,False))

    def test_partial_native_input_failure_still_attempts_keyup(self):
        self.api.fail_down = True
        with self.assertRaises(RuntimeError): self.guard.pulse([87],.5)
        self.assertEqual(self.api.events,[(87,True),(87,False)])
        self.assertFalse(self.guard.armed)

    def test_unfocused_and_unpermitted_arm_send_nothing(self):
        self.guard.armed = False
        self.api.focus = False
        self.guard.arm()
        self.assertFalse(self.guard.armed)
        self.api.focus = True
        self.guard.arm_allowed = False
        self.guard.arm()
        self.assertFalse(self.guard.armed)
        self.assertFalse(self.api.events)

    def test_long_pulse_rejected_before_keydown(self):
        with self.assertRaises(ValueError): self.guard.pulse([87],2)
        self.assertFalse(self.api.events)


def frame(seq=1):
    return {'schema':1,'backend':'client_input_probe','generation':'123','seq':seq,
            'player_guid':'456','map':389,'instance':1,'supported':True,'ready':True,
            'features':[0,0,0,0,1,1,1,0,1,1,1,0,0,0,1,0]}


class ProbeTests(unittest.TestCase):
    def test_replay_both_architectures_preserves_checkpoint_and_never_creates_input_or_soap(self):
        for size in (16,26):
            with self.subTest(size=size), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                checkpoint = root/'navigation.pt'
                torch.save({'schema':1,'algorithm':'ppo','architecture':[size,64,64,9],
                            'version':66,'model':FrozenNavigation(size).state_dict()},checkpoint)
                original = checkpoint.read_bytes()
                replay = root/'states.json'; replay.write_text(json.dumps([frame(),frame(2)]))
                args = argparse.Namespace(checkpoint=checkpoint,replay=replay,execute=False,pid=None,
                    player_guid=None,bindings=None,keys=None,credentials=None,url='unused',seconds=30,
                    steps=2,pulse=.5,seed=1,greedy=False,log=root/'probe.jsonl')
                with patch('client_probe.make_soap',side_effect=AssertionError('No network in replay')), \
                     patch('client_input.WindowsInput',side_effect=AssertionError('No input in replay')):
                    run(args)
                self.assertEqual(checkpoint.read_bytes(),original)
                rows=[json.loads(x) for x in args.log.read_text().splitlines()]
                self.assertEqual(sum(x['event']=='decision' for x in rows),2)
                self.assertFalse(any(x.get('sent_input') for x in rows))
                self.assertEqual(rows[0]['checkpoint']['version'],66)

    def test_wrong_backend_and_nonfinite_inputs_rejected(self):
        value = frame(); value['backend']='server_spline'
        with self.assertRaises(ValueError): validate_observation(value)
        value = frame(); value['features'][0]=float('nan')
        with self.assertRaises(ValueError): validate_observation(value)

    def test_binding_validation_excludes_chat_and_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'keys.json'
            for mapping in ({'forward':13,'back':83,'left':81,'right':69},
                            {'forward':87,'back':87,'left':81,'right':69}):
                p.write_text(json.dumps(mapping))
                with self.assertRaises(ValueError): read_bindings(p)

    def test_nonfinite_and_wrong_checkpoints_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'bad.pt'
            saved={'schema':1,'algorithm':'ppo','architecture':[26,64,64,9],
                   'model':FrozenNavigation(26).state_dict()}
            saved['model']['actor.weight'][0,0]=float('nan')
            torch.save(saved,p)
            with self.assertRaises(ValueError): load_frozen(p)
            saved['architecture']=[16,12,8,6]; torch.save(saved,p)
            with self.assertRaises(ValueError): load_frozen(p)

    def test_live_dryrun_uses_only_read_observations_and_generation_scoped_stop(self):
        class FakeSoap:
            def __init__(self): self.calls=[]
            def command(self,command):
                self.calls.append(command)
                return json.dumps(frame(len(self.calls)))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); p=root/'navigation.pt'
            torch.save({'schema':1,'algorithm':'ppo','architecture':[26,64,64,9],
                        'model':FrozenNavigation(26).state_dict()},p)
            args=argparse.Namespace(checkpoint=p,replay=None,execute=False,pid=None,player_guid=None,
                seconds=2,steps=1,pulse=.001,seed=1,greedy=True,log=root/'probe.jsonl')
            client=FakeSoap()
            with patch('client_probe.make_soap',return_value=client): run(args)
            self.assertEqual(client.calls,['wai client observe','wai client stop 123'])


if __name__=='__main__': unittest.main()
