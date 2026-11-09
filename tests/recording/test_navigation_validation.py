"""Offline compatibility checks for compact navigation captures."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/recording'))
from validate_observation import validate


def navigation_visit():
    snapshot = {'map': 349, 'instance': 1, 'self': {'actor': 1, 'position': [1, 2, 3, 0.5],
                'alive': True, 'combat': True, 'casting': False}, 'party': [], 'selected': None}
    return [
        {'schema': 1, 'seq': 1, 'time_ms': 1, 'episode': 1, 'kind': 'observer_selection',
         'status': 'active_human', 'actor': 1},
        {'schema': 1, 'seq': 2, 'time_ms': 2, 'episode': 1, 'kind': 'episode_start',
         'map': 349, 'instance': 1, 'profile': 'navigation'},
        {'schema': 1, 'seq': 3, 'time_ms': 3, 'episode': 1, 'kind': 'state', 'state': snapshot},
        {'schema': 1, 'seq': 4, 'time_ms': 4, 'episode': 1, 'kind': 'input', 'category': 'movement',
         'source': 'human_request', 'state_phase': 'pre_handler', 'state': snapshot},
        {'schema': 1, 'seq': 5, 'time_ms': 5, 'episode': 1, 'kind': 'episode_end',
         'reason': 'observer_changed'},
        {'schema': 1, 'kind': 'writer_summary', 'dropped': 0},
    ]


class NavigationValidationTests(unittest.TestCase):
    def test_compact_state_is_compatible_but_not_training_ready(self):
        result = validate(navigation_visit())
        self.assertEqual(result['counts']['state'], 1)
        self.assertEqual(result['counts']['input'], 1)
        self.assertEqual(result['unmatched_cast_events'], 0)
        self.assertTrue(result['clean_shutdown'])
        self.assertFalse(result['training_ready'])

    def test_navigation_still_requires_position_and_facing(self):
        records = copy.deepcopy(navigation_visit())
        records[2]['state']['self']['position'] = [1, 2, 3]
        with self.assertRaisesRegex(ValueError, 'position/facing'):
            validate(records)


if __name__ == '__main__':
    unittest.main()
