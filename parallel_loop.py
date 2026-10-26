"""Synchronous on-policy collection across independent dungeon parties; one model writer."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
import time

from torch.utils.tensorboard import SummaryWriter

from live import Soap
from experiment_history import record_training
from rl_loop import (SCHEMA, GAMEPLAY_OUTCOMES, initialize, observation, rollout,
                     atomic_json, checkpoint, notify, update)


class PartySoap:
    def __init__(self, soap, party):
        if not 1 <= party <= 4:
            raise ValueError('Invalid party number')
        self.soap, self.party = soap, party

    def command(self, command):
        if not command.startswith('waiepisode '):
            raise ValueError('Only episode commands may be scoped to a party')
        return self.soap.command(f'waiepisode party {self.party} ' + command[len('waiepisode '):])


def collect_party(client, model, version, counts):
    deadline = time.monotonic() + 150
    while True:
        current = observation(client)
        if current.get('mode') != 'cloned':
            raise RuntimeError('Parallel learning requires independent cloned parties')
        if current['phase'] == 'finished':
            raise RuntimeError('Party finished before collection; inspect its episode')
        if current['phase'] == 'running':
            return rollout(client, model, version, current, counts)
        if time.monotonic() > deadline:
            raise RuntimeError('Party setup did not finish; batch stopped')
        time.sleep(.25)


def merge_counts(baseline, party_counts):
    merged = dict(baseline)
    for counts in party_counts:
        for key, value in counts.items():
            increment = value - baseline.get(key, 0)
            if increment < 0:
                raise ValueError('Exploration counts moved backwards')
            merged[key] = merged.get(key, 0) + increment
    return merged


def run_parallel(args):
    model, optimizer, path, version, _, counts = initialize(args)
    soap = Soap(args.url, os.environ.get('WAI_SOAP_USER'), os.environ.get('WAI_SOAP_PASSWORD'), 'waiepisode ')
    clients = [PartySoap(soap, party) for party in range(1, args.parties + 1)]
    writer = SummaryWriter(str(args.home / 'tensorboard'))
    batches = 0
    try:
        while not args.episodes or batches < args.episodes:
            # Inference threads share immutable weights; no optimizer runs until all futures finish.
            model.eval()
            snapshots = [dict(counts) for _ in clients]
            results = [None] * len(clients)
            with ThreadPoolExecutor(max_workers=len(clients), thread_name_prefix='wai-party') as executor:
                jobs = {executor.submit(collect_party, client, model, version, snapshot): i
                        for i, (client, snapshot) in enumerate(zip(clients, snapshots))}
                try:
                    for future in as_completed(jobs):
                        results[jobs[future]] = future.result()
                except Exception:
                    # Stop every environment before joining workers, rather than leave another run acting.
                    soap.command('waiepisode stop')
                    for future in jobs:
                        future.cancel()
                    raise
            batch = []
            manifest = []
            artifacts = []
            batch_directory = args.home / 'batches' / f'v{version + 1}'
            if batch_directory.exists():
                raise RuntimeError('Unconsumed batch directory exists; refusing duplicate training')
            for party, (transitions, terminal) in enumerate(results, 1):
                episode = terminal['episode']
                if not episode or any(c not in '0123456789-' for c in episode):
                    raise ValueError('Invalid episode identifier')
                directory = args.home / 'episodes' / episode
                trajectory = directory / 'trajectory.json'
                if trajectory.exists():
                    raise RuntimeError('Episode trajectory already exists; inspect before retrying')
                artifacts.append((directory, trajectory, transitions, terminal))
            for party, (directory, trajectory, transitions, terminal) in enumerate(artifacts, 1):
                atomic_json(trajectory, {'schema': SCHEMA, 'version': version, 'party': party,
                                        'terminal': terminal, 'transitions': transitions})
                sha = hashlib.sha256(trajectory.read_bytes()).hexdigest()
                entry = {'schema': SCHEMA, 'episode': terminal['episode'], 'party': party,
                         'policy_version': version, 'sha256': sha}
                atomic_json(directory / 'complete.json', entry)
                manifest.append(entry)
                if terminal['outcome'] not in GAMEPLAY_OUTCOMES or not transitions or not transitions[-1]['done']:
                    raise RuntimeError('Batch quarantined: an environment did not yield a complete gameplay rollout')
                batch.extend(transitions)
            # PPO's GAE calculation resets at each terminal transition, preventing cross-party credit leakage.
            batch_manifest = batch_directory / 'complete.json'
            atomic_json(batch_manifest, {'schema': SCHEMA, 'policy_version': version, 'parties': manifest})
            batch_sha = hashlib.sha256(batch_manifest.read_bytes()).hexdigest()
            for client, (_, terminal) in zip(clients, results):
                notify(client, 'started', terminal['episode'], version)
            try:
                model.train()
                metrics, summary = update(model, optimizer, batch)
                model.eval()
                merged = merge_counts(counts, snapshots)
                checkpoint(path, model, optimizer, version + 1, results[0][1]['episode'], batch_sha, merged)
                counts = merged
            except Exception:
                for client, (_, terminal) in zip(clients, results):
                    notify(client, 'failed', terminal['episode'], version)
                raise
            version += 1
            for client, (directory, _, _, terminal) in zip(clients, artifacts):
                atomic_json(directory / 'trained.json', {'checkpoint_version': version, 'batch_sha256': batch_sha})
                notify(client, 'completed', terminal['episode'], version)
            atomic_json(batch_directory / 'trained.json', {'checkpoint_version': version})
            atomic_json(batch_directory / 'summary.json', summary)
            report = [summary[key] for key in ('samples', 'return', 'weight_delta', 'action',
                      'probability_before', 'probability_after', 'health', 'combat')]
            command = f'waiepisode training report {results[0][1]["episode"]} v{version} ' + ' '.join(map(str, report))
            if clients[0].command(command) != 'WAI_TRAINING_REPORT_RECORDED':
                raise RuntimeError('Shared checkpoint saved but server summary rejected; cycling stopped')
            for key, number in metrics.items():
                writer.add_scalar('train/' + key, number, version)
            writer.add_scalar('batch/parties', len(clients), version)
            writer.add_scalar('batch/return', summary['return'], version)
            writer.add_scalar('batch/clear_fraction', sum(t['outcome'] == 'completed_all_bosses'
                              for _, t in results) / len(clients), version)
            for party, (_, terminal) in enumerate(results, 1):
                writer.add_scalar(f'party_{party}/seconds', terminal['elapsed_ms'] / 1000, version)
                writer.add_scalar(f'party_{party}/deaths', terminal['deaths'], version)
                writer.add_scalar(f'party_{party}/unique_cells', terminal['exploration']['unique_cells'], version)
            writer.flush()
            record_training(args.home, version, results, summary)
            print(f'Shared checkpoint v{version} trained from {len(clients)} parties and {len(batch)} decisions.',
                  flush=True)
            batches += 1
            if args.episodes and batches >= args.episodes:
                break
            if not all(observation(client).get('auto_repeat', False) for client in clients):
                return
            previous = [terminal['episode'] for _, terminal in results]
            for client in clients:
                client.command('waiepisode repeat')
            deadline = time.monotonic() + 150
            while True:
                current = [observation(client) for client in clients]
                if all(value['episode'] != old and value['phase'] in {'loading', 'entering', 'running'}
                       for value, old in zip(current, previous)):
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError('Not all parallel parties restarted; cycling stopped')
                time.sleep(.25)
    except Exception:
        # Infrastructure failures must not silently leave other actors running or train partial batches.
        try:
            soap.command('waiepisode stop')
        except Exception:
            pass
        raise
    finally:
        writer.close()
