"""Bounded navigation behavior cloning; never replaces a source/PPO checkpoint."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import torch
from torch.nn import functional as F
from filelock import FileLock

from client_probe import load_frozen

CONTRACT = 'client_strafe_v1'


def read_samples(path, inputs):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line.strip()]
    if not records or records[0].get('event') != 'session_start' or records[0].get('action_contract') != CONTRACT:
        raise ValueError('Not a compatible client demonstration recording')
    samples = []
    for record in records:
        if record.get('event') != 'demonstration':
            continue
        values, action = record.get('features'), record.get('action')
        if (record.get('source') != 'human' or record.get('stable') is not True or
                type(action) is not int or not 1 <= action <= 8 or
                not isinstance(values, list) or len(values) != inputs or
                any(type(x) not in (float, int) or not math.isfinite(x) for x in values)):
            raise ValueError('Invalid demonstration sample')
        samples.append((values, action))
    if len(samples) < 32:
        raise ValueError('Record at least 32 stable movement samples before training')
    return samples


def fit(source, recording, destination, epochs=5, rate=0.0001):
    source, recording, destination = map(Path, (source, recording, destination))
    if destination.exists() or destination.resolve() in (source.resolve(), recording.resolve()):
        raise ValueError('Output must be a new file, separate from source and recording')
    if not 1 <= epochs <= 20 or not 0 < rate <= .001:
        raise ValueError('Use 1..20 epochs and learning rate at most .001')
    torch.set_num_threads(1)
    model, metadata = load_frozen(source)
    model.requires_grad_(True).train()
    # Critic is deliberately untouched: these are demonstrations, not reward/value targets.
    model.critic.requires_grad_(False)
    samples = read_samples(recording, model.inputs)
    x = torch.tensor([s[0] for s in samples], dtype=torch.float32)
    y = torch.tensor([s[1] for s in samples], dtype=torch.long)
    optimizer = torch.optim.Adam([p for p in model.parameters() if p.requires_grad], lr=rate)
    with torch.no_grad():
        before = float(F.cross_entropy(model(x), y))
    for _ in range(epochs):
        for start in range(0, len(x), 128):
            loss = F.cross_entropy(model(x[start:start+128]), y[start:start+128])
            if not torch.isfinite(loss):
                raise ValueError('Non-finite training loss; no checkpoint saved')
            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1)
            optimizer.step()
    with torch.no_grad():
        after = float(F.cross_entropy(model(x), y))
    if not math.isfinite(after) or after > before:
        raise ValueError('Training loss did not improve; source retained, no output saved')
    payload = {'schema': 1, 'algorithm': 'client_bc', 'architecture': [model.inputs,64,64,9],
               'model': model.state_dict(), 'version': str(metadata['version']) + '-client-bc',
               'action_contract': CONTRACT, 'parent_sha256': metadata['sha256'],
               'demonstration_sha256': hashlib.sha256(recording.read_bytes()).hexdigest(),
               'samples': len(samples), 'loss_before': before, 'loss_after': after}
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, suffix='.pt', delete=False) as stream:
            temporary = Path(stream.name)
            torch.save(payload, stream)
            stream.flush()
            os.fsync(stream.fileno())
        load_frozen(temporary)
        with FileLock(str(destination)+'.lock', timeout=0):
            if destination.exists():
                raise ValueError('Another writer claimed the output; source retained')
            os.rename(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    summary = {k:payload[k] for k in ('samples','loss_before','loss_after','version')}
    print('CLIENT LEARNING COMPLETE:', json.dumps(summary))
    print('Source checkpoint preserved. Lower demonstration loss does not prove better dungeon performance.')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', required=True, type=Path)
    parser.add_argument('--recording', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--epochs', type=int, default=5)
    args = parser.parse_args()
    fit(args.checkpoint, args.recording, args.output, args.epochs)
