"""Checkpoint selection tests; no game or production checkpoint changes."""
import json
from pathlib import Path
import tempfile
import unittest

import torch

from client_probe import FrozenNavigation
from client_tracks import read_tracks, load_track


class TrackTests(unittest.TestCase):
    def save(self, path, algorithm):
        torch.save({'schema':1,'algorithm':algorithm,'architecture':[16,64,64,9],
                    'version':1,'action_contract':'client_strafe_v1',
                    'model':FrozenNavigation(16).state_dict()},path)

    def test_each_track_requires_its_own_training_method(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            algorithms={'il':'client_bc','rl':'ppo','hybrid':'client_mixed'}
            paths={track:root/(track+'.pt') for track in algorithms}
            for track,algorithm in algorithms.items():self.save(paths[track],algorithm)
            for track in paths:
                _,metadata=load_track(paths,track)
                self.assertEqual(metadata['track'],track)
                self.assertEqual(metadata['algorithm'],algorithms[track])
            with self.assertRaises(ValueError):load_track({'il':paths['rl']},'il')
            with self.assertRaises(ValueError):load_track({'hybrid':paths['il']},'hybrid')

    def test_missing_track_never_falls_back(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'models.json'
            path.write_text(json.dumps({'il':None,'rl':'rl.pt','hybrid':None}))
            paths=read_tracks(path)
            self.assertEqual(paths,{'rl':root/'rl.pt'})
            with self.assertRaises(ValueError):load_track(paths,'hybrid')
            with self.assertRaises(ValueError):load_track(paths,'il')

    def test_shared_paths_and_unknown_labels_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'models.json'
            for value in ({'il':'same.pt','rl':'same.pt','hybrid':None},
                          {'il':None,'rl':None,'other':None}):
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):read_tracks(path)


if __name__=='__main__':unittest.main()
