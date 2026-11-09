"""Sanitized, retryable chronological records for independent client policy updates."""
from datetime import datetime, timezone
import json
from pathlib import Path

from filelock import FileLock
from experiment_history import git, REPOSITORY, HISTORY
from client_tracks import TRACK_NAMES


def record_client_training(home,digest,reports,repository=REPOSITORY):
    try:
        queue=Path(home)/'client-history-pending'
        updated=any(r.get('status')=='updated' for r in reports.values())
        if not updated and not queue.exists():return True
        queue.mkdir(parents=True,exist_ok=True)
        entry={'key':digest,'recorded_utc':datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
               'tracks':reports}
        receipt=queue/(digest+'.json')
        if updated and not receipt.exists():receipt.write_text(json.dumps(entry,allow_nan=False),encoding='utf-8')
        with FileLock(str(repository/'.wai-history.lock'),timeout=5):
            if git(repository,'diff','--name-only','--diff-filter=U'):
                raise RuntimeError('Unresolved repository operation')
            for operation in ('MERGE_HEAD','CHERRY_PICK_HEAD','rebase-merge','rebase-apply'):
                if (repository/git(repository,'rev-parse','--git-path',operation)).exists():
                    raise RuntimeError('History waits for Git operation')
            history=repository/HISTORY
            content=history.read_bytes() if history.exists() else b'# WAI experiments history\n'
            files=sorted(queue.glob('*.json'))
            for file in files:
                item=json.loads(file.read_text())
                marker='<!-- wai-client-training:'+item['key']+' -->'
                if marker.encode() in content:continue
                lines=['',marker,'### '+item['recorded_utc']+' UTC - client policy maintenance','']
                for track,report in item['tracks'].items():
                    if report['status']=='updated':
                        lines.append('- '+TRACK_NAMES[track]+': version '+str(report['version'])+
                                     ', reward decisions '+str(report['reinforcement_decisions'])+
                                     ', demonstration samples '+str(report['demonstration_samples'])+
                                     ', changed parameters '+str(report['weights_changed'])+'.')
                lines+=['','Separate checkpoints saved. Updates alone do not prove improved play.','']
                content+='\n'.join(lines).encode('ascii')
            history.write_bytes(content)
            git(repository,'add','--',HISTORY)
            if git(repository,'diff','--cached','--name-only','--',HISTORY):
                git(repository,'commit','--only','-m','docs(Experiments): Record independent client policy updates','--',HISTORY)
            for file in files:file.unlink()
        return True
    except Exception:
        print('Client experiment history pending; saved checkpoints retained; retry on later maintenance.')
        return False
