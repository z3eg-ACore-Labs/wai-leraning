"""Numerical checks for the learner; no server or game is started."""
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from train import features, forward, gradients, initialize, load_records


class LearningTests(unittest.TestCase):
    def test_gradient_matches_finite_difference(self):
        model = initialize(3)
        x = np.random.default_rng(5).uniform(.1, .9, (4, 16))
        y = np.array([0, 1, 2, 0])
        _, grads = gradients(model, x, y)
        for layer in range(3):
            for parameter, index in [(0, (0, 0)), (1, (0,))]:
                p = model[layer][parameter]
                original = p[index]
                epsilon = 1e-6
                p[index] = original + epsilon
                plus = gradients(model, x, y)[0]
                p[index] = original - epsilon
                minus = gradients(model, x, y)[0]
                p[index] = original
                self.assertAlmostEqual((plus-minus)/(2*epsilon), grads[layer][parameter][index], places=5)

    def test_softmax_and_serialized_inference(self):
        model = initialize(6)
        x = np.ones((2, 16))
        reconstructed = [[np.array(p) for p in layer] for layer in json.loads(
            json.dumps([[p.tolist() for p in layer] for layer in model]))]
        a = forward(model, x)[-1]
        np.testing.assert_allclose(a.sum(1), 1)
        np.testing.assert_allclose(a, forward(reconstructed, x)[-1])

    def test_partial_tail_allowed_but_bad_middle_rejected(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as d:
            p = Path(d) / 'events.jsonl'
            p.write_bytes(b'{"kind":"state"}\n{"kind":')
            rows, info = load_records(p)
            self.assertEqual(len(rows), 1)
            self.assertTrue(info['incomplete_tail_ignored'])
            p.write_bytes(b'{broken}\n{"kind":"state"}\n')
            with self.assertRaises(json.JSONDecodeError):
                load_records(p)

    def test_features_do_not_memorize_local_ids_or_world_origin(self):
        me = {'actor': 1, 'position': [10, 20, 0, 0], 'health': 8, 'max_health': 10,
              'power': 5, 'max_power': 10, 'auras': [5487]}
        target = {'actor': 9, 'position': [13, 24, 0, 0], 'health': 3, 'max_health': 5,
                  'alive': True}
        state = {'self': me, 'party': [], 'selected': target}
        first = features(state)
        me['actor'] += 100
        target['actor'] += 100
        for u in (me, target):
            u['position'][0] += 1000
            u['position'][1] -= 2000
        np.testing.assert_allclose(first, features(state))


if __name__ == '__main__':
    unittest.main()
