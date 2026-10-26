"""Append sanitized experiment results and commit only the public history document."""
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

from filelock import FileLock

REPOSITORY = Path(__file__).resolve().parent
HISTORY = 'wai-experiments-history.md'
OUTCOMES = {'completed_all_bosses', 'wipe', 'timeout', 'member_left_instance', 'wandering_off_limits'}


def git(repository, *arguments):
    result = subprocess.run(['git', '-C', str(repository), *arguments], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=15)
    if result.returncode:
        raise RuntimeError('History Git operation failed')
    return result.stdout.strip()


def finite(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError('Non-finite history metric')
    return number


def training_entry(version, results, summary):
    if not isinstance(version, int) or version < 1:
        raise ValueError('Invalid checkpoint version')
    parties = []
    identities = []
    for party, (transitions, terminal) in enumerate(results, 1):
        identities.append(str(terminal['episode']))
        parties.append({'party': party, 'outcome': terminal['outcome'] if terminal['outcome'] in OUTCOMES
                        else 'infrastructure_failure', 'bosses': int(terminal['bosses']),
                        'deaths': int(terminal['deaths']), 'seconds': finite(terminal['elapsed_ms']) / 1000,
                        'decisions': len(transitions),
                        'inputs': len(transitions[0]['state']) if transitions else 0,
                        'new_cells': int(terminal.get('exploration', {}).get('unique_cells', 0)),
                        'exploration_bonus': finite(terminal.get('exploration', {}).get('bonus', 0))})
    # The marker deduplicates retries without publishing raw episode IDs, logs or machine paths.
    key = hashlib.sha256(json.dumps([version, identities]).encode()).hexdigest()
    return {'key': key, 'version': version, 'recorded_utc': datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
            'parties': parties, 'return': finite(summary['return']),
            'weight_delta': finite(summary['weight_delta'])}


def render(entry):
    lines = [f'\n<!-- wai-training:{entry["key"]} -->',
             f'### {entry["recorded_utc"]} UTC — checkpoint v{entry["version"]}', '',
             '| Party | Outcome | Bosses | Deaths | Seconds | Decisions | Inputs | Visited cells | Exploration bonus |',
             '|---:|---|---:|---:|---:|---:|---:|---:|---:|']
    for party in entry['parties']:
        lines.append('| {party} | {outcome} | {bosses}/4 | {deaths} | {seconds:.1f} | {decisions} | '
                     '{inputs} | {new_cells} | {exploration_bonus:.3f} |'.format(**party))
    lines += ['', f'Total training reward: {entry["return"]:.4f}; weight-change L2: {entry["weight_delta"]:.6f}.',
              'Checkpoint saved and training completed. Weight changes alone do not establish improved play.', '']
    return '\n'.join(lines)


def append_and_commit(repository, entries):
    if git(repository, 'diff', '--name-only', '--diff-filter=U'):
        raise RuntimeError('History waits for unresolved Git conflicts')
    for operation in ('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'rebase-merge', 'rebase-apply'):
        if (repository / git(repository, 'rev-parse', '--git-path', operation)).exists():
            raise RuntimeError('History waits for the current Git operation')
    history = repository / HISTORY
    content = history.read_text(encoding='utf-8') if history.exists() else '# WAI experiments history\n'
    for entry in entries:
        if f'<!-- wai-training:{entry["key"]} -->' not in content:
            content += render(entry)
    temporary = history.with_suffix('.md.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(history)
    git(repository, 'add', '--', HISTORY)
    changed = git(repository, 'diff', '--cached', '--name-only', '--', HISTORY)
    if changed:
        # --only keeps other staged/working implementation changes out of these automatic commits.
        git(repository, 'commit', '--only', '-m', 'docs(Experiments): Record completed model training', '--', HISTORY)


def record_training(home, version, results, summary, repository=REPOSITORY):
    """History failure never invalidates an already saved model; retain an entry for retry."""
    pending = None
    try:
        entry = training_entry(version, results, summary)
        queue = Path(home) / 'history-pending'
        queue.mkdir(parents=True, exist_ok=True)
        pending = queue / (entry['key'] + '.json')
        temporary = pending.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(entry, allow_nan=False), encoding='utf-8')
        temporary.replace(pending)
        with FileLock(str(repository / '.wai-history.lock'), timeout=5):
            files = sorted(queue.glob('*.json'))
            entries = sorted((json.loads(path.read_text()) for path in files), key=lambda item: item['recorded_utc'])
            append_and_commit(repository, entries)
            for path in files:
                path.unlink()
        print('Experiment history updated and committed locally.', flush=True)
        return True
    except Exception:
        print('Experiment history update pending; saved model retained. History will retry after later training.',
              flush=True)
        return False
