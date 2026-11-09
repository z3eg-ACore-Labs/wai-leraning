"""Manually launched loopback helper. Only an explicit in-game lease permits movement."""
import argparse
import json
import os
from pathlib import Path
import time
from concurrent.futures import ThreadPoolExecutor

from live import Soap
from navigation_model import select_policy


def observe(soap):
    reply = soap.command('wainav observe')
    if 'NAV_INACTIVE' in reply:
        return None
    row = json.loads(reply)
    if (row.get('schema') != 1 or row.get('contract') != 'navigation_delta_v1' or
            any(type(row.get(k)) is not int or row[k] <= 0 for k in ('generation', 'seq', 'map', 'instance')) or
            type(row.get('difficulty')) is not int or row['difficulty'] not in (0, 1) or
            any(type(row.get(k)) is not bool for k in ('running', 'combat', 'busy'))):
        raise ValueError('Invalid server navigation observation')
    return row


def command(soap, row, verb, suffix=''):
    return soap.command(f"wainav {verb} {row['generation']} {row['seq']}" + (' ' + suffix if suffix else ''))


def run(soap, models, log, alive=lambda: True, recordings=()):
    import torch
    torch.set_num_threads(1)
    current = None
    policy = None
    row = None
    last_position = None
    last_progress = time.monotonic()
    best_index = -1
    route_progress = time.monotonic()
    unavailable = False
    log = Path(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    worker = ThreadPoolExecutor(max_workers=1)
    maintenance = None
    next_scan = 0
    try:
        print('Navigation helper ready; waiting for an explicit in-game navigation request.', flush=True)
        while alive():
            if recordings and time.monotonic() >= next_scan and (maintenance is None or maintenance.done()):
                if maintenance is not None:
                    try:
                        for result in maintenance.result():
                            print('Navigation training: ' + json.dumps(result), flush=True)
                    except Exception as error:
                        print(f'Navigation maintenance failed ({type(error).__name__}); existing model retained.', flush=True)
                from navigation_maintenance import maintain
                maintenance = worker.submit(maintain, recordings, models)
                next_scan = time.monotonic() + 30
            try:
                row = observe(soap)
                unavailable = False
            except (OSError, RuntimeError, ValueError) as error:
                if not unavailable:
                    print(f'Navigation endpoint unavailable ({type(error).__name__}); waiting for server/configuration.', flush=True)
                unavailable = True
                current = policy = last_position = row = None
                time.sleep(1)
                continue
            if row is None:
                current = policy = last_position = None
                time.sleep(0.25)
                continue
            scope = (row['generation'], row['map'], row['instance'], row['difficulty'])
            try:
                if scope != current:
                    policy = select_policy(models, row['map'], row['difficulty'], row['position'])
                    current = scope
                    last_progress = time.monotonic()
                    route_progress = time.monotonic()
                    best_index = -1
                    last_position = None
                if not row['running']:
                    if policy.step(row['position']) is None:
                        command(soap, row, 'release')
                        continue
                    receipt = command(soap, row, 'ready')
                elif row['combat'] or row['busy']:
                    last_progress = time.monotonic()
                    route_progress = time.monotonic()
                    last_position = None
                    time.sleep(0.2)
                    continue
                else:
                    import math
                    position = row['position']
                    if last_position is None or math.dist(position[:3], last_position[:3]) >= 0.5:
                        last_position = position
                        last_progress = time.monotonic()
                    if time.monotonic() - last_progress > 8:
                        raise ValueError('No movement progress; returning control to human')
                    target = policy.step(position)
                    if policy.index is not None and policy.index > best_index + 2:
                        best_index = policy.index
                        route_progress = time.monotonic()
                    if time.monotonic() - route_progress > 20:
                        raise ValueError('No route progress; returning control to human')
                    if target is None:
                        receipt = command(soap, row, 'release')
                    else:
                        receipt = command(soap, row, 'step', ' '.join(f'{v:.5f}' for v in target))
                with log.open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps(dict(map=row['map'], generation=row['generation'],
                                                 seq=row['seq'], receipt=receipt,
                                                 provenance='model_navigation_native_combat')) + '\n')
            except (ValueError, OSError, KeyError, TypeError) as error:
                command(soap, row, 'release')
                print(f'Navigation released: {type(error).__name__}. Check model and support; record to teach more.', flush=True)
                current = policy = None
            time.sleep(0.2)
    finally:
        # Never renew a lease merely to release it: use only the last observed generation/sequence.
        if row:
            try:
                command(soap, row, 'release')
            except Exception:
                pass  # Server independently expires the lease within three seconds.
        worker.shutdown(wait=True, cancel_futures=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, default=Path(__file__).parent / 'data/navigation/models')
    parser.add_argument('--log', type=Path, default=Path(__file__).parent / 'data/navigation/decisions.jsonl')
    parser.add_argument('--url', default='http://127.0.0.1:7878/')
    parser.add_argument('--owner-pid', type=int, default=0)
    parser.add_argument('--recordings', type=Path, action='append', default=[])
    args = parser.parse_args()
    kernel = handle = None
    alive = lambda: True
    if args.owner_pid:
        import ctypes
        from ctypes import wintypes as W
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        kernel.OpenProcess.restype = W.HANDLE
        kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
        kernel.CloseHandle.argtypes = [W.HANDLE]
        handle = kernel.OpenProcess(0x100000, False, args.owner_pid)
        if not handle:
            raise RuntimeError('Owning server is unavailable')
        alive = lambda: kernel.WaitForSingleObject(handle, 0) == 0x102
    try:
        from filelock import FileLock
        args.models.parent.mkdir(parents=True, exist_ok=True)
        with FileLock(str(args.models.parent / 'helper.lock'), timeout=0):
            run(Soap(args.url, os.environ.get('WAI_SOAP_USER'), os.environ.get('WAI_SOAP_PASSWORD'),
                     command_prefix='wainav '), args.models, args.log, alive, args.recordings)
    finally:
        if handle:
            kernel.CloseHandle(handle)
