"""Offline optional-validator boundary; no recording, model training or server."""
from pathlib import Path
import os
import tempfile
import unittest
from unittest.mock import patch
from train import load_validator


class ModuleAdapterTests(unittest.TestCase):
    def test_default_owned_validator_and_missing_explicit_path(self):
        with patch.dict(os.environ, {'WAI_OBSERVATION_VALIDATOR': ''}):
            self.assertFalse(load_validator()([])['training_ready'])
            with self.assertRaisesRegex(ValueError, 'Supply --validator'):
                load_validator(Path('missing-wai-validator-fixture.py'))

    def test_supported_and_unsupported_validator_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'validator.py'
            source.write_text('OBSERVATION_VALIDATION_API = 1\ndef validate(records): return {"rows": len(records)}\n')
            self.assertEqual(load_validator(source)([1, 2]), {'rows': 2})
            source.write_text('OBSERVATION_VALIDATION_API = 2\ndef validate(records): return {}\n')
            # Remove import-cache ambiguity by using a distinct path/version fixture.
            unsupported = source.with_name('unsupported.py')
            unsupported.write_text(source.read_text())
            with self.assertRaisesRegex(ValueError, 'API v1'):
                load_validator(unsupported)

    def test_environment_path_and_missing_callable(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'contract.py'
            source.write_text('OBSERVATION_VALIDATION_API = 1\nvalidate = None\n')
            with patch.dict(os.environ, {'WAI_OBSERVATION_VALIDATOR': str(source)}):
                with self.assertRaisesRegex(ValueError, 'API v1'):
                    load_validator()


if __name__ == '__main__':
    unittest.main()
