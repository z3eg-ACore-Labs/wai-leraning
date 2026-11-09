"""Synthetic schema fixtures only; not gameplay or server integration tests."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/recording'))
from validate_observation import validate


def fixtures():
    actor = {'actor': 1, 'position': [0, 1, 2, 0], 'auras': []}
    request = {'schema': 1, 'seq': 1, 'time_ms': 10, 'kind': 'input', 'source': 'human_request',
               'state_phase': 'pre_handler', 'spell': 123, 'cast_count': 1,
               'state': {'self': actor, 'party': [], 'selected': None}}
    result = {'schema': 1, 'seq': 2, 'time_ms': 11, 'kind': 'cast_go', 'request_seq': 1,
              'spell': 123, 'cast_count': 1}
    return [request, result, {'schema': 1, 'kind': 'writer_summary', 'dropped': 0}]


class ValidationTests(unittest.TestCase):
    def test_valid(self):
        result = validate(fixtures())
        self.assertTrue(result['clean_shutdown'])
        self.assertEqual(result['unmatched_cast_events'], 0)
        self.assertFalse(result['training_ready'])

    def test_future_reference(self):
        records = fixtures()
        records[1]['request_seq'] = 3
        with self.assertRaisesRegex(ValueError, 'future'):
            validate(records)

    def test_wrong_spell(self):
        records = fixtures()
        records[1]['spell'] = 124
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            validate(records)

    def test_post_action_snapshot_rejected(self):
        records = fixtures()
        records[0]['state_phase'] = 'post_handler'
        with self.assertRaisesRegex(ValueError, 'pre-handler'):
            validate(records)

    def test_drops_explicit(self):
        records = fixtures()
        records[1]['seq'] = 3
        records[2]['dropped'] = 1
        self.assertEqual(validate(records)['missing_sequences'], 1)
        records[2]['dropped'] = 0
        with self.assertRaisesRegex(ValueError, 'losses'):
            validate(records)

    def test_uncorrelated_proc_not_human_action(self):
        records = fixtures()
        records[1]['request_seq'] = 0
        self.assertEqual(validate(records)['unmatched_cast_events'], 1)

    def test_private_fields_rejected(self):
        records = fixtures()
        records[0]['state']['self']['account_id'] = 10
        with self.assertRaisesRegex(ValueError, 'private'):
            validate(records)

    def test_nonfinite_rejected(self):
        records = fixtures()
        records[0]['state']['self']['position'][0] = float('nan')
        with self.assertRaisesRegex(ValueError, 'Non-finite'):
            validate(records)

    def test_crash_is_not_clean_shutdown(self):
        self.assertFalse(validate(fixtures()[:-1])['clean_shutdown'])

    def test_clock_order(self):
        records = fixtures()
        records[1]['time_ms'] = 1
        with self.assertRaisesRegex(ValueError, 'backward'):
            validate(records)

    def test_multiple_dungeon_runs(self):
        records = []
        for episode in (1, 2):
            start = {'schema': 1, 'seq': len(records) + 1, 'time_ms': 10, 'kind': 'episode_start', 'episode': episode}
            request = fixtures()[0]
            request.update(seq=len(records) + 2, episode=episode)
            end = {'schema': 1, 'seq': len(records) + 3, 'time_ms': 11, 'kind': 'episode_end', 'episode': episode}
            if episode == 2:
                start['time_ms'] = request['time_ms'] = end['time_ms'] = 12
            records.extend([start, request, end])
        result = validate(records)
        self.assertEqual(result['episodes'], 2)
        self.assertFalse(result['incomplete_episode'])

    def test_wrong_episode(self):
        request = fixtures()[0]
        request.update(seq=2, episode=2)
        start = {'schema': 1, 'seq': 1, 'time_ms': 10, 'kind': 'episode_start', 'episode': 1}
        with self.assertRaisesRegex(ValueError, 'outside'):
            validate([start, request])

    def test_unclosed_episode(self):
        start = {'schema': 1, 'seq': 1, 'time_ms': 10, 'kind': 'episode_start', 'episode': 1}
        self.assertTrue(validate([start])['incomplete_episode'])

    def test_automatic_human_selection(self):
        request = fixtures()[0]
        request['seq'] = 2
        selected = {'schema': 1, 'seq': 1, 'time_ms': 9, 'kind': 'observer_selection',
                    'status': 'active_human', 'actor': 1}
        self.assertEqual(validate([selected, request])['counts']['input'], 1)

    def test_multiple_humans_pause_capture(self):
        request = fixtures()[0]
        request['seq'] = 2
        selected = {'schema': 1, 'seq': 1, 'time_ms': 9, 'kind': 'observer_selection',
                    'status': 'multiple_humans', 'actor': 0}
        with self.assertRaisesRegex(ValueError, 'unselected'):
            validate([selected, request])

    def test_character_handoff(self):
        selected = {'schema': 1, 'seq': 1, 'time_ms': 9, 'kind': 'observer_selection',
                    'status': 'active_human', 'actor': 2}
        request = fixtures()[0]
        request['seq'] = 2
        with self.assertRaisesRegex(ValueError, 'unselected'):
            validate([selected, request])
        request['state']['self']['actor'] = 2
        self.assertEqual(validate([selected, request])['counts']['input'], 1)

    def test_missing_human_not_selected(self):
        selected = {'schema': 1, 'seq': 1, 'time_ms': 9, 'kind': 'observer_selection',
                    'status': 'active_human', 'actor': 0}
        with self.assertRaisesRegex(ValueError, 'unambiguous'):
            validate([selected])


if __name__ == '__main__':
    unittest.main()
