"""Frozen navigation-policy client-input probe. Dry-run by default; never trains or sends server actions."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import time

import torch
from torch import nn
from filelock import FileLock

from exploration import VisitMemory
from live import Soap

DIRECTION_KEYS = ((), ('forward',), ('forward', 'left'), ('left',), ('back', 'left'),
                  ('back',), ('back', 'right'), ('right',), ('forward', 'right'))


class FrozenNavigation(nn.Module):
    def __init__(self, inputs):
        super().__init__()
        self.inputs = inputs
        self.encoder = nn.Sequential(nn.Linear(inputs, 64), nn.Tanh(), nn.Linear(64, 64), nn.Tanh())
        self.actor = nn.Linear(64, 9)
        self.critic = nn.Linear(64, 1)

    def forward(self, values):
        return self.actor(self.encoder(values))


def load_frozen(path):
    # One open handle: source hash and weights describe the same atomic checkpoint generation.
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        stream.seek(0)
        saved = torch.load(stream, map_location='cpu', weights_only=True)
    architecture = saved.get('architecture')
    if saved.get('schema') != 1 or saved.get('algorithm') not in ('ppo', 'client_bc', 'client_mixed') or architecture not in ([16,64,64,9], [26,64,64,9]):
        raise ValueError('Expected a WAI navigation PPO or client demonstration checkpoint (16 or 26 inputs)')
    if saved.get('algorithm') in ('client_bc', 'client_mixed') and saved.get('action_contract') != 'client_strafe_v1':
        raise ValueError('Client checkpoint has an incompatible movement contract')
    model = FrozenNavigation(architecture[0])
    model.load_state_dict(saved['model'], strict=True)
    if any(not torch.isfinite(p).all() for p in model.parameters()):
        raise ValueError('Non-finite checkpoint weights')
    model.eval().requires_grad_(False)
    return model, {'sha256': digest, 'version': saved.get('version'), 'architecture': architecture,
                   'algorithm': saved.get('algorithm')}


def validate_observation(value):
    raw = value.get('features')
    if value.get('schema') != 1 or value.get('backend') != 'client_input_probe':
        raise ValueError('Wrong observation backend: use the rebuilt client-input lease')
    if (not isinstance(raw, list) or len(raw) != 16 or
            any(isinstance(x, bool) or not isinstance(x, (int,float)) or not math.isfinite(x) for x in raw)):
        raise ValueError('Invalid client probe features')
    if (not isinstance(value.get('seq'), int) or value['seq'] <= 0 or
            not isinstance(value.get('generation'), str) or not value['generation'].isdigit() or
            not str(value.get('player_guid', '')).isdigit() or not value.get('instance') or
            not isinstance(value.get('ready'), bool) or not isinstance(value.get('supported'), bool)):
        raise ValueError('Invalid client lease identity')
    return raw


def choose(model, values, ready, generator, greedy=False):
    if not ready:
        return 0, [1.0] + [0.0]*8
    with torch.inference_mode():
        logits = model(torch.tensor(values, dtype=torch.float32))
        if not torch.isfinite(logits).all():
            raise ValueError('Non-finite policy output')
        probabilities = torch.softmax(logits, dim=-1)
        action = int(probabilities.argmax()) if greedy else int(torch.multinomial(probabilities, 1, generator=generator))
    return action, probabilities.tolist()


def read_bindings(path):
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    return validate_bindings(value)


def validate_bindings(value):
    if not isinstance(value, dict):
        raise ValueError('Expected a movement binding mapping')
    if set(value) != {'forward','back','left','right'}:
        raise ValueError('Configure exactly forward/back/left/right movement virtual keys')
    allowed = set(range(0x41,0x5B)) | {0x25,0x26,0x27,0x28}
    if any(type(v) is not int or v not in allowed for v in value.values()) or len(set(value.values())) != 4:
        raise ValueError('Use four distinct letter/arrow keys; reserved takeover/chat keys are forbidden')
    return value


def make_soap(args):
    user, password = os.environ.get('WAI_SOAP_USER'), os.environ.get('WAI_SOAP_PASSWORD')
    if args.credentials:
        if user or password:
            raise ValueError('Use either a complete environment pair or a credentials file, not both')
        value = json.loads(args.credentials.read_text(encoding='utf-8-sig'))
        user, password = value.get('Username'), value.get('Password')
    return Soap(args.url, user, password)


def run(args):
    if not 0 < args.seconds <= 120 or not 0 < args.pulse <= 1 or not 1 <= args.steps <= 200:
        raise ValueError('Probe limited to 120 seconds, 200 decisions, one-second movement pulses')
    if args.execute and (args.replay or not args.pid or not args.player_guid or not args.bindings):
        raise ValueError('Execution requires an explicit client PID, player GUID and verified bindings; no replay')
    torch.set_num_threads(1)
    model, metadata = load_frozen(args.checkpoint)
    generator = torch.Generator().manual_seed(args.seed)
    memory = VisitMemory()
    frames = iter(json.loads(args.replay.read_text(encoding='utf-8-sig'))) if args.replay else None
    soap = None if frames is not None else make_soap(args)
    native = None
    generation = None
    identity = None
    sequence = 0
    lease_to_release = None
    args.log.parent.mkdir(parents=True, exist_ok=True)
    lock = FileLock(str(args.log.parent / ('client-' + str(args.pid or 'dry') + '.lock')), timeout=0)
    with lock, args.log.open('x', encoding='utf-8', buffering=1) as log:
        def receipt(event, **value):
            log.write(json.dumps({'event':event, 'monotonic':time.monotonic(), **value}, allow_nan=False)+'\n')
        receipt('probe_start', checkpoint=metadata, execution=args.execute, backend='client_input_probe', training=False,
                pulse_seconds=args.pulse, boss_progress_available=False, episode_time_available=False)
        try:
            if args.execute:
                from client_input import WindowsInput
                native = WindowsInput(args.pid)
            print('Frozen probe ready. Grant .wai client start on the test character. No server actions or training.')
            if native:
                print('Close chat/menus, verify the character and bindings, focus WoW and press F8. F12 or manual input stops.')
            started = time.monotonic()
            decisions = 0
            previous = None
            while decisions < args.steps and time.monotonic()-started < args.seconds:
                if native and (native.error or native.guard.reason):
                    raise RuntimeError('Input guard stopped: ' + str(native.guard.reason or 'watchdog_failure'))
                requested = time.monotonic()
                if frames is not None:
                    try:
                        obs = next(frames)
                    except StopIteration:
                        break
                else:
                    value = soap.command('wai client observe')
                    if value == 'WAI_INACTIVE':
                        if generation:
                            raise RuntimeError('Client observation lease ended')
                        time.sleep(.1)
                        continue
                    obs = json.loads(value)
                if time.monotonic()-requested > 1:
                    raise RuntimeError('Observation too old for client input')
                raw = validate_observation(obs)
                key = (obs['generation'],str(obs['player_guid']),obs['map'],obs['instance'])
                if identity is not None and key != identity:
                    raise RuntimeError('Control identity or instance changed')
                if obs['seq'] <= sequence:
                    raise RuntimeError('Stale observation sequence')
                if args.player_guid and str(obs['player_guid']) != args.player_guid:
                    raise RuntimeError('Observation belongs to another character')
                identity = key
                generation = obs['generation']
                lease_to_release = generation
                sequence = obs['seq']
                if not obs['supported']:
                    raise RuntimeError('Unsupported movement state: grounded walking probe only')
                if previous is None:
                    print('Observed player GUID:', obs['player_guid'], 'map:', obs['map'], 'instance:', obs['instance'])
                memory.enter(raw, previous, eligible=False)
                values = raw if model.inputs == 16 else memory.features(raw)
                if native:
                    native.guard.arm_allowed = obs['ready']
                    if not native.guard.armed:
                        time.sleep(.1)
                        previous = raw
                        continue
                action, probabilities = choose(model, values, obs['ready'], generator, args.greedy)
                receipt('decision', sequence=sequence, features=values, action=action, probabilities=probabilities,
                        pose=[x*1000 for x in raw[:3]], previous_pose=[x*1000 for x in previous[:3]] if previous else None,
                        ready=obs['ready'], sent_input=bool(native and action), observed_moving=bool(raw[15]))
                if native and action:
                    native.guard.pulse([args.keys[k] for k in DIRECTION_KEYS[action]], args.pulse)
                previous = raw
                decisions += 1
                # Continue observations during a pulse; the native watchdog releases keys even if SOAP stalls.
                deadline = time.monotonic()+args.pulse
                while frames is None and time.monotonic() < deadline:
                    if native and (native.error or native.guard.reason):
                        raise RuntimeError('Input guard stopped: ' + str(native.guard.reason or 'watchdog_failure'))
                    time.sleep(.01)
            receipt('probe_end', decisions=decisions, status='bounded_probe_completed')
        except BaseException:
            receipt('probe_end', status='interrupted_or_failed')
            raise
        finally:
            if native:
                native.close()
            if soap and lease_to_release:
                try:
                    soap.command('wai client stop ' + lease_to_release)
                except Exception:
                    print('Observation lease will expire independently; client input has been released.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--replay', type=Path, help='Offline JSON array of client-probe observations; never sends input')
    parser.add_argument('--execute', action='store_true', help='Explicit bounded foreground input, armed with F8')
    parser.add_argument('--pid', type=int)
    parser.add_argument('--player-guid')
    parser.add_argument('--bindings', type=Path)
    parser.add_argument('--credentials', type=Path)
    parser.add_argument('--url', default='http://127.0.0.1:7878/')
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--steps', type=int, default=30)
    parser.add_argument('--pulse', type=float, default=.5)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--greedy', action='store_true')
    parser.add_argument('--log', type=Path, default=Path('data/client-probe') / (time.strftime('%Y%m%d-%H%M%S')+'.jsonl'))
    args = parser.parse_args()
    args.keys = read_bindings(args.bindings) if args.bindings else None
    run(args)


if __name__ == '__main__':
    main()
