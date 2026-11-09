"""Small neural behavior-cloning policy; anchors bound support, not playback actions."""
import argparse
import json
import math
from pathlib import Path
import os
import copy

import torch
from torch import nn

from navigation_data import read_demonstration

CONTRACT = 'navigation_delta_v1'


def network():
    return nn.Sequential(nn.Linear(16, 64), nn.Tanh(), nn.Linear(64, 64), nn.Tanh(), nn.Linear(64, 3))


def features(position, progress, center, scale):
    return ([(position[i] - center[i]) / scale[i] for i in range(3)] +
            [math.sin(position[3]), math.cos(position[3]), progress] +
            [trig(2 * math.pi * frequency * progress) for frequency in (1, 2, 4, 8, 16)
             for trig in (math.sin, math.cos)])


def train(recording, destination, map_id, difficulty=0, epochs=500):
    torch.set_num_threads(1)
    torch.manual_seed(29)
    rows, digest = read_demonstration(recording, map_id, difficulty)
    positions = torch.tensor([row[0][:3] for row in rows], dtype=torch.float32)
    center = positions.mean(0).tolist()
    scale = positions.std(0).clamp(min=1).tolist()
    inputs = torch.tensor([features(p, i / (len(rows) - 1), center, scale)
                           for i, (p, _) in enumerate(rows)], dtype=torch.float32)
    # Packet cadence is not a reliable speed demonstration. Learn travel direction;
    # the executor bounds distance, and ordinary server movement supplies speed.
    targets = torch.tensor([[v / math.sqrt(sum(x*x for x in velocity)) for v in velocity]
                            for _, velocity in rows], dtype=torch.float32)
    # Random held-out samples measure interpolation only, not a new dungeon or full-run success.
    order = torch.randperm(len(rows))
    cut = max(1, len(rows) // 5)
    test, fit = order[:cut], order[cut:]
    model = network()
    optimizer = torch.optim.Adam(model.parameters(), lr=0.003)
    best_loss, best_state = float('inf'), None
    for _ in range(epochs):
        optimizer.zero_grad()
        loss = nn.functional.mse_loss(model(inputs[fit]), targets[fit])
        loss.backward()
        optimizer.step()
        if _ % 10 == 0 or _ == epochs - 1:
            with torch.inference_mode():
                candidate = nn.functional.mse_loss(model(inputs[test]), targets[test]).item()
            if math.isfinite(candidate) and candidate < best_loss:
                best_loss, best_state = candidate, copy.deepcopy(model.state_dict())
    if best_state is None:
        raise ValueError('No finite validation checkpoint')
    model.load_state_dict(best_state)
    with torch.inference_mode():
        mse = nn.functional.mse_loss(model(inputs[test]), targets[test]).item()
    if not math.isfinite(mse):
        raise ValueError('Training produced nonfinite validation loss')
    artifact = dict(schema=1, contract=CONTRACT, architecture=[16, 64, 64, 3], map=map_id,
                    difficulty=difficulty, center=center, scale=scale,
                    anchors=[p for p, _ in rows], source_sha256=digest,
                    layers=[dict(weight=layer.weight.detach().tolist(), bias=layer.bias.detach().tolist())
                            for layer in (model[0], model[2], model[4])],
                    validation=dict(samples=len(rows), held_out=len(test), direction_mse=mse,
                                    scope='same-recording interpolation; not live or completion validation'),
                    provenance='human_observer', action_semantics='unit_direction',
                    accepted=mse <= 0.20, completion_verified=False)
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.tmp')
    temporary.write_text(json.dumps(artifact, allow_nan=False), encoding='utf-8')
    os.replace(temporary, destination)
    return artifact['validation']


class Policy:
    def __init__(self, path, map_id, difficulty):
        path = Path(path)
        if path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError('Model exceeds size limit')
        data = json.loads(path.read_text(encoding='utf-8'))
        if (type(data.get('schema')) is not int or data['schema'] != 1 or data.get('contract') != CONTRACT or
                data.get('architecture') != [16, 64, 64, 3] or
                data.get('map') != map_id or data.get('difficulty') != difficulty or
                data.get('provenance') != 'human_observer' or data.get('action_semantics') != 'unit_direction' or
                data.get('accepted') is not True):
            raise ValueError('Incompatible navigation checkpoint')
        validation = data.get('validation', {})
        error = validation.get('direction_mse')
        if (type(error) not in (int, float) or not math.isfinite(error) or not 0 <= error <= 0.20):
            raise ValueError('Checkpoint did not pass the offline direction-error gate')
        self.center, self.scale = data['center'], data['scale']
        self.anchors = data['anchors']
        if not 64 <= len(self.anchors) <= 100000:
            raise ValueError('Invalid checkpoint support')
        for values, length in [(self.center, 3), (self.scale, 3)] + [(p, 4) for p in self.anchors]:
            if (not isinstance(values, list) or len(values) != length or
                    any(type(x) not in (int, float) or abs(x) > 1e9 or not math.isfinite(x) for x in values)):
                raise ValueError('Invalid checkpoint numbers')
        if any(x < 1 for x in self.scale):
            raise ValueError('Invalid normalization scale')
        self.model = network()
        if len(data['layers']) != 3:
            raise ValueError('Invalid layer count')
        with torch.no_grad():
            for layer, saved in zip((self.model[0], self.model[2], self.model[4]), data['layers']):
                weight = torch.tensor(saved['weight'], dtype=torch.float32)
                bias = torch.tensor(saved['bias'], dtype=torch.float32)
                if weight.shape != layer.weight.shape or bias.shape != layer.bias.shape:
                    raise ValueError('Invalid layer shapes')
                if not torch.isfinite(weight).all() or not torch.isfinite(bias).all():
                    raise ValueError('Nonfinite parameters')
                layer.weight.copy_(weight)
                layer.bias.copy_(bias)
        self.model.eval()
        self.index = None

    def support_distance(self, position):
        return min(math.dist(position[:3], anchor[:3]) for anchor in self.anchors)

    def step(self, position):
        if (not isinstance(position, list) or len(position) != 4 or
                any(type(x) not in (int, float) or abs(x) > 1e9 or not math.isfinite(x) for x in position)):
            raise ValueError('Invalid live position')
        # Resume near the current location; thereafter preserve local route progress at intersections.
        low = max(0, self.index - 8) if self.index is not None else 0
        high = min(len(self.anchors), self.index + 80) if self.index is not None else len(self.anchors)
        index = min(range(low, high), key=lambda i: math.dist(position[:3], self.anchors[i][:3]))
        distance = math.dist(position[:3], self.anchors[index][:3])
        if distance > 5:
            raise ValueError('Outside demonstrated navigation support; take over and record')
        self.index = index
        if index >= len(self.anchors) - 3 and distance < 1:
            return None
        values = features(position, index / (len(self.anchors) - 1), self.center, self.scale)
        with torch.inference_mode():
            velocity = self.model(torch.tensor(values, dtype=torch.float32)).tolist()
        length = math.sqrt(sum(v * v for v in velocity))
        if not math.isfinite(length) or length < 0.1:
            raise ValueError('Policy has no usable movement prediction')
        seconds = 0.8 / length
        return [position[i] + velocity[i] * seconds for i in range(3)]


def select_policy(models, map_id, difficulty, position):
    root = Path(models) / str(map_id)
    candidates = [root / f'{difficulty}.json']
    candidates += sorted((root / str(difficulty)).glob('*.json'),
                         key=lambda path: path.stat().st_mtime, reverse=True)[:32]
    best, distance = None, 5.0
    for path in candidates:
        try:
            policy = Policy(path, map_id, difficulty)
            support = policy.support_distance(position)
            if support <= distance:
                best, distance = policy, support
        except (OSError, ValueError, TypeError, KeyError):
            continue
    if best is None:
        raise ValueError('No accepted dungeon model covers this position')
    return best


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recording', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--map', required=True, type=int)
    parser.add_argument('--difficulty', default=0, type=int, choices=(0, 1))
    parser.add_argument('--epochs', default=500, type=int)
    args = parser.parse_args()
    print(json.dumps(train(args.recording, args.output, args.map, args.difficulty, args.epochs)))
