"""Resident passive human recorder. Never generates client input or starts reinforcement learning."""
import argparse
import ctypes
from ctypes import wintypes as W
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import secrets
import time

from filelock import FileLock

from client_demonstrations import CONTRACT
from client_input import WindowsInput
from client_probe import make_soap, read_bindings, validate_observation, load_frozen
from client_session import Keyboard, movement_action
from client_tracks import read_tracks
from client_training import maintain
from exploration import VisitMemory
from dungeon_runs import identity as dungeon_identity, completion_proof, policy_home
from client_identity import match_client, read_foreground_marker


class Recorder:
    """Only an exact-instance authoritative completion may enqueue human-only maintenance."""
    def __init__(self, directory, player_guid, inputs, submit):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.guid, self.inputs_for, self.inputs, self.submit = str(player_guid), inputs, 26, submit
        self.stream = self.path = self.identity = None
        self.sequence = 0
        self.memory = VisitMemory()
        self.samples = 0

    def emit(self, event, **fields):
        self.stream.write(json.dumps(dict(event=event, **fields), allow_nan=False) + '\n')
        self.stream.flush()

    def end(self, status, **proof):
        if self.stream:
            path = self.path
            self.emit('session_end', status=status, samples=self.samples, **proof)
            self.stream.close()
            self.stream = None
            self.identity = None
            if status == 'completed':
                print('Human dungeon recording saved; requesting behavior-cloning and human-only hybrid maintenance.', flush=True)
                self.submit(path)
            else:
                print('Incomplete human recording saved; no training.', flush=True)

    def observe(self, packet):
        if (packet.get('schema') != 1 or packet.get('backend') != 'human_dungeon_watch' or
                str(packet.get('player_guid')) != self.guid or type(packet.get('seq')) is not int or
                packet['seq'] <= self.sequence):
            raise ValueError('Invalid passive observation identity or sequence')
        self.sequence = packet['seq']
        run = packet.get('run')
        if run is None:
            return None
        map_id, difficulty = dungeon_identity(packet)
        if (not isinstance(run, str) or not run.isdigit() or len(run) > 32 or
                type(packet.get('instance')) is not int or packet['instance'] <= 0 or
                type(packet.get('completed')) is not bool or type(packet.get('interrupted')) is not bool or
                type(packet.get('bosses')) is not int or not 0 <= packet['bosses'] <= 4):
            raise ValueError('Invalid passive run proof')
        identity = (run, packet['instance'], map_id, difficulty)
        if self.identity and self.identity != identity:
            self.end('incomplete', reason='instance_changed')
        if packet['interrupted'] or packet['state'] in ('blocked', 'offline'):
            self.end('incomplete', reason=packet['state'])
            return None
        if packet['completed']:
            proof = completion_proof(packet)
            if self.identity == identity:
                self.end('completed', outcome='completed_all_bosses' if map_id==389 else 'server_dungeon_completed',
                         run=run, map=map_id, difficulty=difficulty, instance=identity[1], **proof)
            return None
        if packet['state'] != 'active':
            return None  # Leaving temporarily does not manufacture a terminal outcome.
        obs = packet.get('observation', {})
        raw = validate_observation(obs)
        if (str(obs['player_guid']) != self.guid or obs['generation'] != run or obs['map'] != map_id or
                obs['instance'] != identity[1] or obs['seq'] != packet['seq'] or
                obs.get('bosses') != packet['bosses']):
            raise ValueError('Passive snapshot does not match run proof')
        if not self.stream:
            path = self.directory / ('human-' + run + '-' + str(map_id) + '-' + str(difficulty) + '-' + str(identity[1]) + '.jsonl')
            if path.exists():
                return None  # Completed or crashed recordings are immutable; never append/retrain.
            self.path = path
            self.inputs = self.inputs_for(packet) if callable(self.inputs_for) else self.inputs_for
            self.stream = path.open('x', encoding='utf-8', buffering=1)
            self.identity, self.samples, self.memory = identity, 0, VisitMemory()
            self.emit('session_start', action_contract=CONTRACT, collection='automatic_human_dungeon',
                      scope='navigation_only', run=run, map=map_id, difficulty=difficulty, instance=identity[1],
                      initial_completed_mask=packet.get('completed_mask', 0),
                      completion_available=packet.get('completion_available', map_id==389))
            print('Dungeon entry observed: recording human navigation automatically.', flush=True)
        self.memory.enter(raw, eligible=False)
        return raw if self.inputs == 16 else self.memory.features(raw)

    def sample(self, features, keys, stable, ready):
        action = movement_action(keys)
        if self.stream and features is not None and stable and ready and action is not None:
            self.emit('demonstration', source='human', stable=True, features=features, action=action)
            self.samples += 1


def wow_processes():
    """Enumerate names only; do not inspect or operate unrelated processes."""
    class Entry(ctypes.Structure):
        _fields_ = [('size', W.DWORD), ('usage', W.DWORD), ('pid', W.DWORD),
                    ('heap', ctypes.c_size_t), ('module', W.DWORD), ('threads', W.DWORD),
                    ('parent', W.DWORD), ('priority', W.LONG), ('flags', W.DWORD), ('name', W.WCHAR * 260)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [W.HANDLE, ctypes.POINTER(Entry)]
    kernel.CloseHandle.argtypes = [W.HANDLE]
    handle = kernel.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise RuntimeError('Unable to enumerate client processes')
    result = []
    try:
        entry = Entry(); entry.size = ctypes.sizeof(entry)
        found = kernel.Process32FirstW(handle, ctypes.byref(entry))
        while found:
            if entry.name.lower() == 'wow.exe':
                result.append(entry.pid)
            found = kernel.Process32NextW(handle, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(handle)
    return result


def owner_handle(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenProcess.restype = W.HANDLE
    kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
    kernel.CloseHandle.argtypes = [W.HANDLE]
    handle = kernel.OpenProcess(0x100000, False, pid)
    if not handle:
        raise RuntimeError('Owning server is no longer available')
    return kernel, handle


def run(args):
    automatic=getattr(args,'auto_character',False)
    if (not automatic and (not args.player_guid or not args.player_guid.isdigit())) or not 0.1 <= args.pulse <= 1:
        raise ValueError('Select automatic addon identity or a numeric GUID; use 0.1..1 second samples')
    def inputs_for(packet):
        home = policy_home(args.learning_home, packet)
        if not (home/'models.json').exists():
            return 26  # Data collection needs no random production model initialization.
        paths = read_tracks(home/'models.json')
        dimensions = {load_frozen(paths[t])[0].inputs for t in ('il', 'hybrid')}
        if len(dimensions) != 1:
            raise ValueError('Human policies must use matching observation dimensions')
        return dimensions.pop()
    keys = None if automatic else read_bindings(args.bindings)
    bound_marker = None
    bound_guid = args.player_guid or ''
    soap = make_soap(args)
    token = str(secrets.randbits(96))
    kernel, owner = owner_handle(args.owner_pid)
    native = None
    pending = []
    availability = None
    args.log_dir.mkdir(parents=True, exist_ok=True)
    args.learning_home.mkdir(parents=True, exist_ok=True)
    try:
        with FileLock(str(args.learning_home / '.automatic-recorder.lock'), timeout=0), ThreadPoolExecutor(max_workers=1) as worker:
            def submit(path):
                with path.open(encoding='utf-8') as stream:
                    home = policy_home(args.learning_home, json.loads(stream.readline()))
                if not (home/'models.json').exists():
                    print('Recording saved; matching dungeon policy needs explicit initialization before training.', flush=True)
                    return
                print('TRAINING STARTED: behavior cloning and human-only hybrid; reinforcement learning unchanged.', flush=True)
                pending.append(worker.submit(maintain, home, path))
            recorder = Recorder(args.log_dir, bound_guid, inputs_for, submit)
            print('Passive observer waiting for server, client and dungeon entry. F7 pause; F6 resume; F12 stop.', flush=True)
            try:
                while kernel.WaitForSingleObject(owner, 0) == 0x102:
                    for future in list(pending):
                        if future.done():
                            pending.remove(future)
                            try:
                                future.result()
                            except Exception as error:
                                print('Training failed; recording retained for explicit retry:', type(error).__name__, flush=True)
                    candidates = wow_processes()
                    capture_identity = True
                    if automatic:
                        try:
                            request_started=time.monotonic()
                            foreground,marker=read_foreground_marker(candidates)
                            discovery=json.loads(soap.command('wai client discover'))
                            if time.monotonic()-request_started>1:
                                raise ValueError('Stale client discovery')
                            candidate=match_client(discovery,marker)
                            registration=match_client(discovery,bound_marker,require_dungeon=False)
                            if recorder.stream and (registration is None or registration['player_guid']!=bound_guid or
                                    registration['bindings']!=keys or (native and foreground==native.pid and candidate and
                                    candidate['player_guid']!=bound_guid)):
                                recorder.end('incomplete',reason='client_identity_changed_or_expired')
                                bound_marker=None
                            if not recorder.stream:
                                if candidate:
                                    if bound_marker!=marker or bound_guid!=candidate['player_guid']:
                                        recorder.sequence=0
                                    bound_marker,bound_guid=marker,candidate['player_guid']
                                    recorder.guid=bound_guid
                                    keys=candidate['bindings']
                                    target=foreground
                                else:
                                    target=None
                            else:
                                target=native.pid if native and native.pid in candidates else None
                            capture_identity=bool(target and foreground==target and marker==bound_marker and
                                                  match_client(discovery,bound_marker,require_dungeon=False))
                        except Exception as error:
                            recorder.end('incomplete',reason='identity_discovery_unavailable')
                            bound_marker=None
                            recorder.sequence=0
                            if availability!='identity_unavailable':
                                availability='identity_unavailable'
                                print('Client identity unavailable; no capture:',type(error).__name__,flush=True)
                            time.sleep(.5)
                            continue
                    else:
                        target = args.pid if args.pid in candidates else (candidates[0] if args.pid is None and len(candidates) == 1 else None)
                    if native and target != native.pid:
                        recorder.end('incomplete', reason='client_closed_or_changed')
                        native.close(); native = None
                    if native is None and target:
                        try:
                            native = WindowsInput(target, controls_only=True, read_only=True)
                            keyboard = Keyboard(keys)
                        except (OSError, ValueError, RuntimeError):
                            time.sleep(.5)
                            continue
                    if automatic and native is None:
                        time.sleep(.25)
                        continue
                    if native and (native.stop_requested.is_set() or native.error):
                        break
                    first = keyboard.movement() if native else frozenset()
                    revision = native.input_revision if native else 0
                    started = time.monotonic()
                    try:
                        value = soap.command('wai client watch ' + bound_guid + ' ' + token)
                        if time.monotonic() - started > 1:
                            raise RuntimeError('Stale passive observation')
                        packet = json.loads(value)
                        if availability != packet.get('state'):
                            availability = packet.get('state')
                            print('Passive observer state:', availability, flush=True)
                        # No client means no capture; continue heartbeat and preserve incomplete status.
                        if native is None:
                            recorder.end('incomplete', reason='no_unique_client')
                            time.sleep(.25)
                            continue
                        features = recorder.observe(packet)
                    except Exception as error:
                        recorder.end('incomplete', reason='observation_unavailable')
                        if availability != 'unavailable':
                            availability = 'unavailable'
                            print('Passive observation unavailable; waiting without training:', type(error).__name__, flush=True)
                        recorder.sequence = 0  # A restarted server has a fresh sequence/run identity.
                        time.sleep(.5)
                        continue
                    controls = native.take_controls()
                    if controls:
                        native.recording_paused = controls != {0x75}  # F6 only; model keys pause.
                    stable = (capture_identity and native.focused() and not native.recording_paused and
                              not keyboard.interfering() and time.monotonic() > native.injected_until)
                    deadline = time.monotonic() + args.pulse
                    while time.monotonic() < deadline and kernel.WaitForSingleObject(owner, 0) == 0x102:
                        stable = stable and (native.focused() and not native.recording_paused and
                                             native.input_revision == revision and keyboard.movement() == first and
                                             not keyboard.interfering() and time.monotonic() > native.injected_until)
                        if native.stop_requested.is_set() or native.error:
                            stable = False
                            break
                        time.sleep(.01)
                    if automatic:
                        stable=stable and read_foreground_marker(candidates)==(native.pid,bound_marker)
                    obs = packet.get('observation', {})
                    recorder.sample(features, first, stable, obs.get('ready') is True and obs.get('supported') is True)
            finally:
                recorder.end('incomplete', reason='observer_stopped')
                if native:
                    native.close()
    finally:
        kernel.CloseHandle(owner)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--learning-home', type=Path, required=True)
    selection=parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--player-guid',help='Legacy explicit diagnostic identity')
    selection.add_argument('--auto-character',action='store_true',help='Match the foreground addon marker to a fresh server session')
    parser.add_argument('--owner-pid', type=int, required=True, help='Owning server process; stop when it exits')
    parser.add_argument('--pid', type=int, help='Optional specific Wow.exe process; otherwise require exactly one')
    parser.add_argument('--bindings', type=Path, help='Legacy fixed identity bindings; automatic mode uses live addon bindings')
    parser.add_argument('--credentials', type=Path)
    parser.add_argument('--url', default='http://127.0.0.1:7878/')
    parser.add_argument('--pulse', type=float, default=.25)
    parser.add_argument('--log-dir', type=Path, default=Path(__file__).parent / 'data/client-probe')
    run(parser.parse_args())


if __name__ == '__main__':
    main()
