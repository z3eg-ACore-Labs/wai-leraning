"""Human navigation-only maintenance; no game commands or reinforcement learning."""
import json
from pathlib import Path

from navigation_model import train


def maintain(recordings, models):
    models = Path(models)
    ledger_path = models.parent / 'processed-recordings.json'
    try:
        ledger = json.loads(ledger_path.read_text(encoding='utf-8'))
    except FileNotFoundError:
        ledger = {}
    results = []
    for root in recordings:
        # Bound each pass; this runs in a worker, never in the movement/heartbeat path.
        for source in sorted(Path(root).rglob('*.jsonl'), key=lambda p: p.stat().st_mtime)[-500:]:
            stamp = f'{source.stat().st_mtime_ns}:{source.stat().st_size}'
            key = str(source.resolve())
            if ledger.get(key) == stamp or source.stat().st_size > 128 * 1024 * 1024:
                continue
            try:
                with source.open(encoding='utf-8-sig') as stream:
                    header = next((row for _, line in zip(range(20), stream)
                                   if (row := json.loads(line)).get('kind') == 'episode_start'), None)
                if not header:
                    continue
                map_id, difficulty = header.get('map'), header.get('difficulty', 0)
                if type(map_id) is not int or map_id <= 0 or type(difficulty) is not int or difficulty not in (0, 1):
                    raise ValueError('Invalid recorded scope')
                candidate = models.parent / 'candidate.json'
                try:
                    metrics = train(source, candidate, map_id, difficulty)
                except ValueError as error:
                    if 'still open' in str(error):
                        continue  # Revisit when the recorder closes the file.
                    raise
                artifact = json.loads(candidate.read_text(encoding='utf-8'))
                saved = models.parent / 'experiments' / (artifact['source_sha256'] + '.json')
                saved.parent.mkdir(parents=True, exist_ok=True)
                candidate.replace(saved)
                promoted = False
                destination = models / str(map_id) / f'{difficulty}.json'
                previous = json.loads(destination.read_text(encoding='utf-8')) if destination.exists() else {}
                if artifact['accepted']:
                    memory = models / str(map_id) / str(difficulty) / saved.name
                    memory.parent.mkdir(parents=True, exist_ok=True)
                    prior_memory = json.loads(memory.read_text(encoding='utf-8')) if memory.exists() else {}
                    if (prior_memory.get('accepted') is not True or
                            artifact['validation']['direction_mse'] < prior_memory['validation']['direction_mse']):
                        temporary_memory = memory.with_suffix('.pending')
                        temporary_memory.write_text(json.dumps(artifact, allow_nan=False), encoding='utf-8')
                        temporary_memory.replace(memory)
                if artifact['accepted'] and (previous.get('accepted') is not True or
                        artifact['validation']['samples'] > previous['validation']['samples'] or
                        (artifact['validation']['samples'] == previous['validation']['samples'] and
                         artifact['validation']['direction_mse'] < previous['validation']['direction_mse'])):
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    temporary = destination.with_suffix('.pending')
                    temporary.write_text(json.dumps(artifact, allow_nan=False), encoding='utf-8')
                    temporary.replace(destination)
                    promoted = True
                results.append(dict(map=map_id, **metrics, accepted=artifact['accepted'], promoted=promoted))
            except (ValueError, OSError, KeyError, TypeError) as error:
                results.append(dict(status='rejected', reason=type(error).__name__))
            ledger[key] = stamp
            ledger_path.parent.mkdir(parents=True, exist_ok=True)
            temporary = ledger_path.with_suffix('.tmp')
            temporary.write_text(json.dumps(ledger), encoding='utf-8')
            temporary.replace(ledger_path)
    return results
