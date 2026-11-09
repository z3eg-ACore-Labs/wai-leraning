"""Foreground navigation: F6 demonstrate, F7 pause, F8 IL, F9 RL, F10 hybrid, F12 stop."""
import argparse
import ctypes
import json
from pathlib import Path
import time

import torch
from filelock import FileLock

from client_demonstrations import CONTRACT, fit
from client_input import GuardedInput, WindowsInput
from client_probe import DIRECTION_KEYS, choose, make_soap, read_bindings, validate_observation
from exploration import VisitMemory
from client_tracks import CONTROL_KEYS, TRACK_KEYS, TRACK_NAMES, load_track, read_tracks
from client_rollout import ClientRollout, counters


def movement_action(names):
    """Only client forward/back/STRAFE combinations; never infer turning or idle."""
    names = frozenset(names)
    return next((i for i, keys in enumerate(DIRECTION_KEYS) if i and frozenset(keys) == names), None)


class Modes:
    def __init__(self, native):
        self.native = native
        self.mode = 'paused'

    def switch(self, mode, ready=False):
        if mode not in ('paused', 'demonstrate', 'model'):
            raise ValueError('Unknown session mode')
        self.native.guard.stop('mode_changed')
        self.mode = 'paused'
        if mode == 'model':
            if not ready or not self.native.focused():
                return self.mode
            # Explicit F8 is the only rearm path after a manual takeover/focus loss.
            self.native.guard = GuardedInput(self.native)
            self.native.guard.arm_allowed = True
            self.native.guard.arm()
            if not self.native.guard.armed:
                return self.mode
        self.mode = mode
        return mode


class Keyboard:
    """Read only configured movement and mode keys; no text or arbitrary key recording."""
    def __init__(self, keys):
        self.keys = keys
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.user.GetAsyncKeyState.argtypes = [ctypes.c_int]
        self.user.GetAsyncKeyState.restype = ctypes.c_short

    def down(self, key):
        return bool(self.user.GetAsyncKeyState(key) & 0x8000)

    def movement(self):
        return frozenset(name for name, key in self.keys.items() if self.down(key))

    def interfering(self):
        # Mouse turning, modifiers and the default turn keys are not supported labels.
        keys = {1,2,4,0x10,0x11,0x12,0x41,0x44} - set(self.keys.values())
        return any(self.down(key) for key in keys)


def run(args):
    if not 1 <= args.seconds <= 3600 or not .1 <= args.pulse <= 1:
        raise ValueError('Session limit: 1..3600 seconds, .1..1 second movement steps')
    if args.train_output and args.train_output.exists():
        raise ValueError('Training output must be a new checkpoint filename')
    learning_home=getattr(args,'learning_home',None)
    learn=getattr(args,'learn',False)
    if learning_home:
        args.models=learning_home/'models.json'
    if learn and (not learning_home or args.train_output):
        raise ValueError('--learn requires --learning-home and cannot combine with --train-output')
    track = getattr(args, 'track', 'rl')
    paths = read_tracks(args.models) if getattr(args, 'models', None) else {track:args.checkpoint}
    model, metadata = load_track(paths, track)
    training_source = paths[track]
    if args.train_output and track != 'il':
        raise ValueError('Automatic demonstration training requires --track il; RL/hybrid are not updated here')
    keys = read_bindings(args.bindings)
    if any(key in (*CONTROL_KEYS,0x7B) for key in keys.values()):
        raise ValueError('Bindings overlap mode controls')
    torch.set_num_threads(1)
    generator = torch.Generator().manual_seed(args.seed)
    memory = VisitMemory()
    soap = make_soap(args)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    identity, sequence, generation = None, 0, None
    samples, completed = 0, False
    with FileLock(str(args.log.parent / ('client-'+str(args.pid)+'.lock')), timeout=0):
        with args.log.open('x', encoding='utf-8', buffering=1) as log:
            def emit(event, **fields):
                log.write(json.dumps({'event':event,'monotonic':time.monotonic(),**fields}, allow_nan=False)+'\n')
            emit('session_start', action_contract=CONTRACT, checkpoint=metadata, scope='navigation_only')
            rollout=ClientRollout(emit)
            native = None
            try:
                native = WindowsInput(args.pid, controls_only=True)
                keyboard, modes = Keyboard(keys), Modes(native)
                held_controls = set()
                started = time.monotonic()
                print('CLIENT SESSION PAUSED. Grant .wai client start in game, then close chat.')
                print('F6: record demonstrations | F7: pause | F12: stop.')
                print('F8: Imitation learning | F9: Reinforcement learning | F10: Hybrid imitation and reinforcement learning.')
                print('Selected track:', TRACK_NAMES[track], '| version:', metadata['version'], '| SHA256:', metadata['sha256'])
                while time.monotonic()-started < args.seconds:
                    if native.error:
                        raise RuntimeError('Native input watchdog failed') from native.error
                    if native.stop_requested.is_set() or keyboard.down(0x7B):
                        completed = True
                        break
                    requested = time.monotonic()
                    first_keys = keyboard.movement()
                    value = soap.command('wai client observe')
                    if native.stop_requested.is_set():
                        break
                    if value == 'WAI_INACTIVE':
                        if identity:
                            raise RuntimeError('Client observation lease ended')
                        time.sleep(.1)
                        continue
                    if time.monotonic()-requested > 1:
                        raise RuntimeError('Observation exceeded freshness limit')
                    obs = json.loads(value)
                    raw = validate_observation(obs)
                    current = (obs['generation'],str(obs['player_guid']),obs['map'],obs['instance'])
                    if str(obs['player_guid']) != args.player_guid or (identity and identity != current):
                        raise RuntimeError('Client identity or instance changed')
                    if obs['seq'] <= sequence or not obs['supported']:
                        terminal=obs.get('outcome') in ('player_died','completed_all_bosses')
                        if obs['seq']<=sequence or not terminal:
                            raise RuntimeError('Stale observation or unsupported movement state')
                    identity, sequence, generation = current, obs['seq'], obs['generation']
                    if learn:counters(obs)
                    interrupted=bool(native.guard.reason or not native.focused())
                    if interrupted:rollout.truncate()
                    pending=rollout.pending
                    bonus=memory.enter(raw,pending['raw'] if pending else None,
                                       eligible=bool(pending and pending['action']))
                    features = raw if model.inputs == 16 else memory.features(raw)
                    rollout.observe(obs,features,bonus)
                    if obs.get('outcome') in ('player_died','completed_all_bosses'):
                        print('CLIENT RUN FINISHED:',obs['outcome'])
                        emit('client_outcome',outcome=obs['outcome'])
                        break
                    controls = {key for key in CONTROL_KEYS if keyboard.down(key)}
                    pressed = (controls - held_controls) | native.take_controls()
                    held_controls = controls
                    if modes.mode == 'model' and (native.guard.reason or not native.focused()):
                        reason = native.guard.reason or 'not_ready_or_focus_lost'
                        modes.switch('paused')
                        emit('mode', mode='paused', reason=reason)
                        print('CLIENT MODEL PAUSED:', reason, '(select a learning method to resume)')
                    if pressed and native.focused():
                        rollout.truncate()
                        # Multiple controls resolve toward pause, never accidental model activation.
                        key = next(iter(pressed)) if len(pressed)==1 else None
                        mode = {0x75:'demonstrate',0x76:'paused',**{k:'model' for k in TRACK_KEYS}}.get(key,'paused')
                        if key in TRACK_KEYS:
                            modes.switch('paused')
                            requested_track = TRACK_KEYS[key]
                            try:
                                if getattr(args,'models',None):paths=read_tracks(args.models)
                                candidate, candidate_metadata = load_track(paths, requested_track)
                            except (ValueError, OSError, RuntimeError) as error:
                                emit('track_unavailable', track=requested_track)
                                print('TRACK UNAVAILABLE:', TRACK_NAMES[requested_track], type(error).__name__, '- CONTROL PAUSED')
                                continue
                            model, metadata, track = candidate, candidate_metadata, requested_track
                            features = raw if model.inputs == 16 else memory.features(raw)
                            emit('track_selected', checkpoint=metadata)
                            print('SELECTED:', TRACK_NAMES[track], '| version:', metadata['version'], '| SHA256:', metadata['sha256'])
                            if time.monotonic()-requested > 1:
                                emit('mode', mode='paused', reason='refresh_required_after_checkpoint_load', track=track)
                                print('CLIENT PAUSED: checkpoint loaded; press its track key again with fresh telemetry.')
                                continue
                        mode = modes.switch(mode, obs['ready'])
                        emit('mode', mode=mode, track=track)
                        print('CLIENT MODE:', mode.upper(), '|', TRACK_NAMES[track])
                    mode = modes.mode
                    action = movement_action(first_keys)
                    stable = first_keys == keyboard.movement() and not keyboard.interfering()
                    if mode == 'model':
                        action, probabilities = choose(model, features, obs['ready'], generator)
                        native.guard.pulse([keys[k] for k in DIRECTION_KEYS[action]], args.pulse)
                        if learn:rollout.start(model,features,raw,obs,track,metadata,action,probabilities)
                        emit('policy_decision', features=features, action=action, source='model',
                             track=track, checkpoint_sha256=metadata['sha256'])
                    deadline = time.monotonic()+args.pulse
                    while time.monotonic() < deadline:
                        if (not native.focused() or keyboard.movement() != first_keys or keyboard.interfering() or
                                any(keyboard.down(k) for k in (*CONTROL_KEYS,0x7B))):
                            stable = False
                        if native.error or native.stop_requested.is_set():
                            break
                        time.sleep(.01)
                    if mode == 'demonstrate' and stable and obs['ready'] and action is not None:
                        emit('demonstration', features=features, action=action, source='human', stable=True)
                        samples += 1
                completed = True
                rollout.truncate()
                emit('session_end', samples=samples, status='completed')
            except BaseException:
                emit('session_end', samples=samples, status='failed_or_interrupted')
                raise
            finally:
                try:
                    if native:
                        native.close()
                finally:
                    if generation:
                        try:
                            soap.command('wai client stop '+generation)
                        except Exception:
                            print('Lease cleanup unavailable; independent server lease expiry remains active.')
    if completed and learn:
        from client_training import maintain
        print('CLIENT TRAINING STARTED: maintaining three isolated policy tracks after input release.')
        maintain(learning_home,args.log)
    elif completed and args.train_output:
        fit(training_source, args.log, args.train_output)
    else:
        print('Session saved; weights unchanged. Human movement samples:', samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--checkpoint', type=Path, help='One checkpoint, validated against --track')
    sources.add_argument('--models', type=Path, help='JSON mapping il/rl/hybrid to separate checkpoints')
    sources.add_argument('--learning-home',type=Path,help='Managed independent client checkpoints and registry')
    parser.add_argument('--learn',action='store_true',help='Maintain eligible independent policies after this session ends')
    parser.add_argument('--track', choices=('il','rl','hybrid'), default='rl', help='Initial track; execution still starts paused')
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--player-guid', required=True)
    parser.add_argument('--bindings', required=True, type=Path, help='Forward/back/STRAFE left/right bindings')
    parser.add_argument('--credentials', type=Path)
    parser.add_argument('--url', default='http://127.0.0.1:7878/')
    parser.add_argument('--seconds', type=float, default=900)
    parser.add_argument('--pulse', type=float, default=.25)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--train-output', type=Path, help='Optional NEW checkpoint, trained only after session ends')
    parser.add_argument('--log', type=Path, default=Path('data/client-probe')/(time.strftime('%Y%m%d-%H%M%S')+'-session.jsonl'))
    run(parser.parse_args())


if __name__ == '__main__':
    main()
