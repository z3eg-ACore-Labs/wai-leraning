"""History commits use isolated temporary Git repositories, never the working project repository."""
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from experiment_history import HISTORY, git, record_training


class HistoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory(dir=Path(__file__).resolve().parent.parent / 'output')
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.repository = root / 'repository'
        self.repository.mkdir()
        self.home = root / 'artifacts'
        git(self.repository, 'init', '-q')
        git(self.repository, 'config', 'user.name', 'WAI Offline Test')
        git(self.repository, 'config', 'user.email', 'offline@example.invalid')
        (self.repository / 'README.md').write_text('Fixture only\n')
        git(self.repository, 'add', 'README.md')
        git(self.repository, 'commit', '-q', '-m', 'Initialize fixture')
        self.results = [([{'state': [0] * 26}], {
            'episode': 'private-episode-identifier', 'outcome': 'timeout', 'bosses': 0,
            'deaths': 1, 'elapsed_ms': 1000, 'character': 'private-character', 'password': 'private-secret',
            'exploration': {'unique_cells': 7, 'bonus': .02}})]
        self.summary = {'return': -50, 'weight_delta': .01}

    def test_commit_excludes_unrelated_staged_and_working_files(self):
        staged = self.repository / 'other-feature.txt'
        staged.write_text('staged user work\n')
        git(self.repository, 'add', staged.name)
        (self.repository / 'README.md').write_text('uncommitted user edits\n')
        self.assertTrue(record_training(self.home, 1, self.results, self.summary, self.repository))
        changed = git(self.repository, 'show', '--pretty=format:', '--name-only', 'HEAD')
        self.assertEqual(changed, HISTORY)
        self.assertEqual(git(self.repository, 'diff', '--cached', '--name-only'), staged.name)
        self.assertIn('README.md', git(self.repository, 'diff', '--name-only'))
        self.assertEqual(staged.read_text(), 'staged user work\n')

    def test_retries_are_deduplicated_and_public_fields_are_sanitized(self):
        self.assertTrue(record_training(self.home, 1, self.results, self.summary, self.repository))
        commit = git(self.repository, 'rev-parse', 'HEAD')
        self.assertTrue(record_training(self.home, 1, self.results, self.summary, self.repository))
        self.assertEqual(git(self.repository, 'rev-parse', 'HEAD'), commit)
        text = (self.repository / HISTORY).read_text(encoding='utf-8')
        self.assertEqual(text.count('checkpoint v1'), 1)
        self.assertNotIn('private-', text)
        self.assertNotIn('password', text)
        self.assertIn('timeout', text)

    def test_git_failure_retains_receipt_then_retries_without_touching_model(self):
        self.home.mkdir()
        model = self.home / 'navigation.pt'
        model.write_bytes(b'unchanged checkpoint')
        with patch('experiment_history.git', side_effect=RuntimeError('fixture failure')):
            self.assertFalse(record_training(self.home, 1, self.results, self.summary, self.repository))
        self.assertEqual(model.read_bytes(), b'unchanged checkpoint')
        self.assertTrue(list((self.home / 'history-pending').glob('*.json')))
        self.assertTrue(record_training(self.home, 1, self.results, self.summary, self.repository))
        self.assertFalse(list((self.home / 'history-pending').glob('*.json')))
        self.assertEqual((self.repository / HISTORY).read_text(encoding='utf-8').count('checkpoint v1'), 1)


if __name__ == '__main__':
    unittest.main()
