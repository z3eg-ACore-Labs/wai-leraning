"""Offline fixtures only: no server, live client, credentials or network."""
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from navigation_data import navigation_point, read_demonstration
from navigation_model import train, Policy
from navigation_controller import observe, command
from navigation_maintenance import maintain
from navigation_model import select_policy

TEST_HOME = Path(__file__).parent / 'data/navigation/test-temp'
TEST_HOME.mkdir(parents=True, exist_ok=True)

def point(i=1, x=None):
    return dict(schema=1, kind='state', episode=1, seq=i+1, time_ms=i*200,
                state=dict(map=349, instance=1, self=dict(alive=True, combat=False, casting=False,
                           position=[float(i if x is None else x), 0., 0., 0.])))


def recording(path, transform=None):
    rows = [dict(schema=1, kind='episode_start', map=349, instance=1, episode=1, difficulty=0,
                 provenance='human')]
    rows += [point(i) for i in range(1, 101)]
    rows += [dict(schema=1, kind='episode_end', episode=1, reason='model_execution'),
             dict(schema=1, kind='writer_summary', dropped=0)]
    if transform:
        transform(rows)
    path.write_text('\n'.join(json.dumps(r) for r in rows), encoding='utf-8')


class DataTests(unittest.TestCase):
    def test_strict_schema_casting_and_numbers(self):
        base = point()
        self.assertIsNotNone(navigation_point(base))
        for key, value in [('schema', True), ('kind', 'input'), ('episode', 0), ('seq', False), ('time_ms', -1)]:
            row = copy.deepcopy(base); row[key] = value
            self.assertIsNone(navigation_point(row))
        for value in [True, '1', None, float('nan'), float('inf'), 10**1000]:
            row = copy.deepcopy(base); row['state']['self']['position'][0] = value
            self.assertIsNone(navigation_point(row))
        row = copy.deepcopy(base); del row['state']['self']['casting']
        self.assertIsNone(navigation_point(row))
        row['state']['self']['casts'] = []
        self.assertIsNotNone(navigation_point(row))
        row['state']['self']['casting'] = False; row['state']['self']['casts'] = [123]
        self.assertIsNone(navigation_point(row))

    def test_copies_without_private_fields(self):
        row = point(); row['state']['self']['actor'] = 123
        result = navigation_point(row)
        result['position'][0] = 99
        self.assertEqual(row['state']['self']['position'][0], 1)
        self.assertNotIn('actor', result)

    def test_closed_human_recording_and_scope(self):
        with tempfile.TemporaryDirectory(dir=TEST_HOME) as directory:
            path = Path(directory)/'visit.jsonl'
            recording(path)
            rows, digest = read_demonstration(path, 349)
            self.assertEqual(len(rows), 99)
            self.assertEqual(len(digest), 64)
            with self.assertRaises(ValueError): read_demonstration(path, 109)
            with self.assertRaises(ValueError): read_demonstration(path, 349, 1)
            recording(path, lambda rows: rows[0].update(provenance='model'))
            with self.assertRaises(ValueError): read_demonstration(path, 349)
            recording(path, lambda rows: rows.pop())
            with self.assertRaises(ValueError): read_demonstration(path, 349)

    def test_never_bridges_combat_death_teleport_or_instance_gap(self):
        for change in [lambda r: r['state']['self'].update(combat=True),
                       lambda r: r['state']['self'].update(alive=False),
                       lambda r: r['state']['self'].update(casting=True),
                       lambda r: r['state'].update(instance=2),
                       lambda r: r['state']['self'].update(position=[10000., 0., 0., 0.])]:
            with tempfile.TemporaryDirectory(dir=TEST_HOME) as directory:
                path = Path(directory)/'visit.jsonl'
                recording(path, lambda rows: change(rows[50]))
                rows, _ = read_demonstration(path, 349)
                self.assertEqual(len(rows), 97)
                self.assertTrue(all(math.sqrt(sum(v*v for v in velocity)) <= 10 for _, velocity in rows))

    def test_trains_loads_predicts_and_rejects_foreign_or_unsupported_model(self):
        with tempfile.TemporaryDirectory(dir=TEST_HOME) as directory:
            source, artifact = Path(directory)/'visit.jsonl', Path(directory)/'model.json'
            recording(source)
            metrics = train(source, artifact, 349, epochs=100)
            self.assertTrue(math.isfinite(metrics['direction_mse']))
            policy = Policy(artifact, 349, 0)
            position = [50., 0., 0., 0.]
            target = policy.step(position)
            self.assertGreater(target[0], position[0])
            self.assertLessEqual(math.dist(target, position[:3]), 2.00001)
            with self.assertRaises(ValueError): Policy(artifact, 109, 0)
            with self.assertRaises(ValueError): Policy(artifact, 349, 1)
            with self.assertRaises(ValueError): policy.step([5000., 0., 0., 0.])
            payload = json.loads(artifact.read_text())
            payload['layers'][0]['bias'][0] = float('nan')
            artifact.write_text(json.dumps(payload))
            with self.assertRaises(ValueError): Policy(artifact, 349, 0)

    def test_helper_observation_and_lease_bound_commands(self):
        class Fake:
            text = ''
            def command(self, text):
                self.text = text
                return self.reply
        soap = Fake(); soap.reply = 'NAV_INACTIVE'
        self.assertIsNone(observe(soap))
        row = dict(schema=1, contract='navigation_delta_v1', generation=9, seq=3,
                   map=349, instance=1, difficulty=0, running=False, combat=False, busy=False,
                   position=[1., 2., 3., 0.])
        soap.reply = json.dumps(row)
        self.assertEqual(observe(soap), row)
        command(soap, row, 'ready')
        self.assertEqual(soap.text, 'wainav ready 9 3')
        row['generation'] = True; soap.reply = json.dumps(row)
        with self.assertRaises(ValueError): observe(soap)

    def test_maintenance_promotes_only_human_closed_data_and_deduplicates(self):
        with tempfile.TemporaryDirectory(dir=TEST_HOME) as directory:
            root = Path(directory)
            captures, models = root/'captures', root/'models'
            captures.mkdir()
            recording(captures/'visit.jsonl')
            results = maintain([captures], models)
            self.assertEqual(len(results), 1)
            self.assertTrue(results[0]['accepted'])
            self.assertTrue(results[0]['promoted'])
            artifact = models/'349/0.json'
            before = artifact.read_bytes()
            self.assertIsInstance(select_policy(models, 349, 0, [50., 0., 0., 0.]), Policy)
            self.assertEqual(maintain([captures], models), [])
            # A repeat training pass with worse validation must preserve the accepted memory.
            payload = json.loads(before)
            memory = models/'349/0'/ (payload['source_sha256'] + '.json')
            memory_before = memory.read_bytes()
            import os
            stamp = (captures/'visit.jsonl').stat()
            os.utime(captures/'visit.jsonl', ns=(stamp.st_atime_ns, stamp.st_mtime_ns + 1000000))
            payload['validation']['direction_mse'] = .19
            def worse_training(source, destination, map_id, difficulty):
                destination.write_text(json.dumps(payload), encoding='utf-8')
                return payload['validation']
            with patch('navigation_maintenance.train', side_effect=worse_training):
                results = maintain([captures], models)
            self.assertFalse(results[0]['promoted'])
            self.assertEqual(artifact.read_bytes(), before)
            self.assertEqual(memory.read_bytes(), memory_before)
            recording(captures/'automated.jsonl', lambda rows: rows[0].update(provenance='model'))
            results = maintain([captures], models)
            self.assertEqual(results[0]['status'], 'rejected')
            self.assertEqual(artifact.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
