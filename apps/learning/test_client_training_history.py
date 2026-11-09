"""History fixtures use temporary files and mocked Git, never the real repository."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from client_training_history import record_client_training


class HistoryTests(unittest.TestCase):
    def report(self):
        return {'hybrid':{'status':'updated','version':1,'reinforcement_decisions':8,
                          'demonstration_samples':40,'weights_changed':5}}

    def git(self,repo,*args):
        if args[0]=='rev-parse':return '.git/'+args[-1]
        if args[:2]==('diff','--cached'):return 'wai-experiments-history.md'
        return ''

    def test_sanitized_append_preserves_old_bytes_and_deduplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);repo=root/'repo';repo.mkdir();home=root/'store';home.mkdir()
            history=repo/'wai-experiments-history.md';prefix=b'# old history\n\x97\n';history.write_bytes(prefix)
            with patch('client_training_history.git',side_effect=self.git):
                self.assertTrue(record_client_training(home,'abc123',self.report(),repo))
                self.assertTrue(record_client_training(home,'abc123',self.report(),repo))
            result=history.read_bytes()
            self.assertTrue(result.startswith(prefix))
            self.assertEqual(result.count(b'wai-client-training:abc123'),1)
            self.assertIn(b'Hybrid imitation and reinforcement learning',result)
            self.assertFalse(list((home/'client-history-pending').glob('*.json')))

    def test_git_failure_retries_even_when_session_is_already_consumed(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);repo=root/'repo';repo.mkdir();home=root/'store';home.mkdir()
            with patch('client_training_history.git',side_effect=RuntimeError('test unavailable git')):
                self.assertFalse(record_client_training(home,'abc123',self.report(),repo))
            self.assertTrue(list((home/'client-history-pending').glob('*.json')))
            with patch('client_training_history.git',side_effect=self.git):
                self.assertTrue(record_client_training(home,'abc123',{'hybrid':{'status':'already_consumed'}},repo))
            self.assertFalse(list((home/'client-history-pending').glob('*.json')))


if __name__=='__main__':unittest.main()
