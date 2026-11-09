"""Synthetic policy tests: never touch a live client, checkpoint or public history."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import torch
from torch.distributions import Categorical

from client_demonstrations import CONTRACT
from client_probe import FrozenNavigation,load_frozen
from client_rollout import ClientRollout,counters,estimate
from client_tracks import read_tracks
from client_training import initialize,maintain,advantages


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


class TrainingTests(unittest.TestCase):
    def setUp(self):
        self.history=patch('client_training_history.record_client_training',return_value=True)
        self.history.start();self.addCleanup(self.history.stop)

    def session(self,home,root,human=True,tracks=('rl','hybrid'),wrong_hash=False):
        paths=read_tracks(home/'models.json')
        records=[{'event':'session_start','action_contract':CONTRACT}]
        if human:
            records += [{'event':'demonstration','source':'human','stable':True,
                         'features':[.1]*16,'action':1} for _ in range(40)]
        for track in tracks:
            model,metadata=load_frozen(paths[track])
            for i in range(8):
                state=[.1+i*.001]*16
                dist=Categorical(logits=model(torch.tensor(state)))
                records.append({'event':'reinforcement_transition','track':track,'action_contract':CONTRACT,
                                'checkpoint_sha256':'bad' if wrong_hash else metadata['sha256'],
                                'state':state,'next_state':state,'action':1,'mask':[True]*9,
                                'reward':float(i+1),'value':estimate(model,state),'next_value':estimate(model,state),
                                'log_probability':float(dist.log_prob(torch.tensor(1))),
                                'done':i==7,'segment':1,'sequence':i+1})
        records.append({'event':'session_end','status':'completed'})
        path=root/'session.jsonl';path.write_text('\n'.join(map(json.dumps,records))+'\n')
        return path

    def test_independent_updates_and_duplicate_consumption(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);home=root/'store'
            initialize(home,inputs=16)
            old=read_tracks(home/'models.json');hashes={k:digest(v) for k,v in old.items()}
            session=self.session(home,root)
            report=maintain(home,session)
            self.assertTrue(all(r['status']=='updated' for r in report.values()))
            self.assertEqual(report['il']['reinforcement_decisions'],0)
            self.assertEqual(report['rl']['demonstration_samples'],0)
            self.assertEqual(report['hybrid']['reinforcement_decisions'],8)
            self.assertEqual(report['hybrid']['demonstration_samples'],40)
            new=read_tracks(home/'models.json')
            for track in old:
                self.assertEqual(digest(old[track]),hashes[track])
                saved=torch.load(new[track],weights_only=True)
                self.assertEqual(saved['parent_sha256'],hashes[track])
                self.assertEqual(saved['version'],1)
            self.assertEqual(len({digest(p) for p in new.values()}),3)
            registry=(home/'models.json').read_bytes()
            again=maintain(home,session)
            self.assertTrue(all(r['status']=='already_consumed' for r in again.values()))
            self.assertEqual(registry,(home/'models.json').read_bytes())

    def test_demonstrations_update_imitation_and_hybrid_but_not_reinforcement(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);home=root/'store';initialize(home,inputs=16)
            old=read_tracks(home/'models.json')
            report=maintain(home,self.session(home,root,tracks=()))
            new=read_tracks(home/'models.json')
            self.assertEqual(new['rl'],old['rl'])
            self.assertEqual(report['rl']['status'],'no_eligible_samples')
            self.assertEqual(report['hybrid']['reinforcement_decisions'],0)
            self.assertNotEqual(new['hybrid'],old['hybrid'])

    def test_other_policy_experience_never_updates_hybrid(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);home=root/'store';initialize(home,inputs=16)
            old=read_tracks(home/'models.json')
            report=maintain(home,self.session(home,root,human=False,tracks=('rl',)))
            new=read_tracks(home/'models.json')
            self.assertEqual(new['il'],old['il']);self.assertEqual(new['hybrid'],old['hybrid'])
            self.assertEqual(report['rl']['status'],'updated')

    def test_provenance_and_publication_failure_preserve_registry(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);home=root/'store';initialize(home,inputs=16)
            registry=(home/'models.json').read_bytes()
            with self.assertRaises(ValueError):maintain(home,self.session(home,root,wrong_hash=True))
            self.assertEqual(registry,(home/'models.json').read_bytes())
            self.assertFalse(any(home.glob('*/v000001.pt')))
            session=self.session(home,root)
            import client_training
            publish=client_training.publish
            calls=[]
            def fail_second(path,payload):
                calls.append(path)
                if len(calls)==2:raise OSError('simulated publication failure')
                publish(path,payload)
            with patch('client_training.publish',side_effect=fail_second):
                with self.assertRaises(OSError):maintain(home,session)
            self.assertEqual(registry,(home/'models.json').read_bytes())
            self.assertFalse(any(home.glob('*/v000001.pt')))
            maintain(home,session)  # A clean retry must work after rollback.

    def test_wrong_probabilities_and_partial_session_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);home=root/'store';initialize(home,inputs=16)
            session=self.session(home,root,human=False)
            records=[json.loads(x) for x in session.read_text().splitlines()]
            records[1]['log_probability']=0
            session.write_text('\n'.join(map(json.dumps,records)))
            with self.assertRaises(ValueError):maintain(home,session)
            records=records[:-1];session.write_text('\n'.join(map(json.dumps,records)))
            with self.assertRaises(ValueError):maintain(home,session)

    def test_initialization_does_not_overwrite_source_or_existing_store(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'source.pt'
            torch.save({'schema':1,'algorithm':'ppo','architecture':[16,64,64,9],
                        'version':99,'model':FrozenNavigation(16).state_dict()},source)
            before=source.read_bytes();initialize(root/'store',source)
            self.assertEqual(before,source.read_bytes())
            with self.assertRaises(ValueError):initialize(root/'store',source)


class RolloutTests(unittest.TestCase):
    def obs(self,seq=1,**kw):
        return dict(boss_progress_available=True,episode_time_available=True,bosses=0,deaths=0,
                    elapsed_ms=seq*1000,outcome='running',seq=seq,ready=True,**kw)

    def test_reward_death_completion_and_terminal_once(self):
        model=FrozenNavigation(16);features=[.1]*16;events=[]
        collector=ClientRollout(lambda event,**kw:events.append((event,kw)))
        probs=torch.softmax(model(torch.tensor(features)),dim=-1).tolist()
        collector.start(model,features,features,self.obs(),'hybrid',{'sha256':'abc'},1,probs)
        end=self.obs(2);end.update(bosses=4,deaths=1,outcome='completed_all_bosses')
        collector.observe(end,features,.02)
        row=events[0][1]
        self.assertEqual(row['next_value'],0);self.assertTrue(row['done'])
        self.assertAlmostEqual(row['reward'],1034.99)
        collector.observe(end,features)
        self.assertEqual(len(events),1)

    def test_manual_takeover_drops_unobserved_action(self):
        model=FrozenNavigation(16);events=[]
        collector=ClientRollout(lambda *a,**k:events.append(k))
        collector.start(model,[0]*16,[0]*16,self.obs(),'rl',{'sha256':'abc'},0,[1]*9)
        collector.truncate();collector.observe(self.obs(2),[0]*16)
        self.assertEqual(events,[])
        with self.assertRaises(ValueError):counters({'boss_progress_available':False})

    def test_truncations_bootstrap_and_segments_do_not_leak(self):
        rows=[{'reward':1,'value':2,'next_value':3,'done':False,'segment':1},
              {'reward':100,'value':0,'next_value':0,'done':True,'segment':2}]
        result=advantages(rows)
        self.assertAlmostEqual(float(result[0]),1+.99*3-2,places=5)
        self.assertEqual(float(result[1]),100)


if __name__=='__main__':unittest.main()
