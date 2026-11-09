"""Explicit checkpoint selection; never aliases a missing learning track."""
import json
from pathlib import Path

from client_probe import load_frozen

ALGORITHMS = {'il': 'client_bc', 'rl': 'ppo', 'hybrid': 'client_mixed'}
TRACK_NAMES = {'il': 'Imitation learning', 'rl': 'Reinforcement learning',
               'hybrid': 'Hybrid imitation and reinforcement learning'}
TRACK_KEYS = {0x77: 'il', 0x78: 'rl', 0x79: 'hybrid'}
CONTROL_KEYS = (0x75, 0x76, *TRACK_KEYS)


def read_tracks(path):
    path = Path(path)
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(value, dict) or set(value) != set(ALGORITHMS):
        raise ValueError('Track configuration must have exactly il, rl and hybrid')
    result = {}
    for track, name in value.items():
        if name is not None and (not isinstance(name, str) or not name.strip()):
            raise ValueError('Each checkpoint must be a filename or null for unavailable')
        if name:
            result[track] = (path.parent / name).resolve()
    if len(set(result.values())) != len(result):
        raise ValueError('IL, RL and hybrid must use separate checkpoint files')
    return result


def load_track(paths, track):
    if track not in ALGORITHMS or track not in paths:
        raise ValueError('Selected track has no configured checkpoint')
    model, metadata = load_frozen(paths[track])
    if metadata['algorithm'] != ALGORITHMS[track]:
        raise ValueError('Checkpoint training method does not match selected track')
    return model, {**metadata, 'track':track}
