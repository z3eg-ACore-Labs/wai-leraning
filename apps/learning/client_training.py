"""Independent client policies: behavior cloning, proximal policy optimization, and their hybrid."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import torch
from torch.distributions import Categorical
from torch.nn import functional as F
from filelock import FileLock

from client_demonstrations import CONTRACT
from client_probe import FrozenNavigation, load_frozen
from client_tracks import ALGORITHMS, TRACK_NAMES, read_tracks
from dungeon_runs import identity as dungeon_identity, completion_proof


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, allow_nan=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def publish(path, payload):
    """New immutable generation only. Registry advancement is a separate final commit."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, suffix='.pt', delete=False) as stream:
            temporary = Path(stream.name)
            torch.save(payload, stream)
            stream.flush()
            os.fsync(stream.fileno())
        load_frozen(temporary)
        if path.exists():
            raise ValueError('Checkpoint generation already exists; inspect orphan before retrying')
        os.rename(temporary, path)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def initialize(home, source=None, inputs=26, seed=7, dungeon_map=389, difficulty=0):
    dungeon = dungeon_identity(dict(map=dungeon_map, difficulty=difficulty))
    home = Path(home).resolve()
    home.mkdir(parents=True, exist_ok=True)
    torch.set_num_threads(1)
    torch.manual_seed(seed)
    with FileLock(str(home/'.training.lock'), timeout=0):
        if (home/'models.json').exists():
            raise ValueError('Client model registry already exists; initialization never resets it')
        if source:
            model, metadata = load_frozen(Path(source))
        else:
            if inputs not in (16,26):
                raise ValueError('Expected 16 or 26 inputs')
            model = FrozenNavigation(inputs)
            metadata = {'sha256':None,'initialization':'random','seed':seed}
        registry = {}
        for track, algorithm in ALGORITHMS.items():
            path = home/track/'v000000.pt'
            payload = {'schema':1,'algorithm':algorithm,'action_contract':CONTRACT,
                       'architecture':[model.inputs,64,64,9],'model':model.state_dict(),
                       'version':0,'initialization':metadata,'processed_sessions':[],
                       'dungeon_identity':list(dungeon),
                       'demonstrations':[], 'optimizer':None, 'track':track,'rng':torch.get_rng_state()}
            publish(path,payload)
            registry[track] = path.relative_to(home).as_posix()
        atomic_json(home/'models.json',registry)
    print('INITIALIZED three separate client policies with a disclosed common starting point; source untouched.')


def value(model, states):
    return model.critic(model.encoder(states)).squeeze(-1)


def validate_transitions(records, track, digest, inputs):
    transitions = [r for r in records if r.get('event')=='reinforcement_transition' and r.get('track')==track]
    last = {}
    for row in transitions:
        if row.get('checkpoint_sha256') != digest or row.get('action_contract') != CONTRACT:
            raise ValueError('On-policy checkpoint/action provenance mismatch')
        for key in ('state','next_state'):
            if (not isinstance(row.get(key),list) or len(row[key])!=inputs or
                    any(type(x) not in (int,float) or not math.isfinite(x) for x in row[key])):
                raise ValueError('Invalid reinforcement state')
        mask, action = row.get('mask'), row.get('action')
        if (not isinstance(mask,list) or len(mask)!=9 or any(type(x) is not bool for x in mask) or
                mask[0] is not True or type(action) is not int or not 0<=action<9 or not mask[action]):
            raise ValueError('Invalid action mask or action')
        if any(type(row.get(k)) not in (int,float) or not math.isfinite(row[k])
               for k in ('reward','value','next_value','log_probability')) or type(row.get('done')) is not bool:
            raise ValueError('Invalid reinforcement targets')
        segment, sequence = row.get('segment'),row.get('sequence')
        if type(segment) is not int or segment<1 or type(sequence) is not int or sequence<=last.get(segment,0):
            raise ValueError('Reordered or duplicate transition')
        last[segment]=sequence
    return transitions


def advantages(rows):
    """Bootstrap truncations; reset between manual handoffs/track switches/true terminals."""
    result=[0.0]*len(rows)
    future=0.0
    for i in reversed(range(len(rows))):
        row=rows[i]
        delta=row['reward'] + (0 if row['done'] else .99*row['next_value'])-row['value']
        continues=(not row['done'] and i+1<len(rows) and rows[i+1]['segment']==row['segment'])
        future=delta+(.99*.95*future if continues else 0)
        result[i]=future
    return torch.tensor(result,dtype=torch.float32)


def human_samples(records, inputs):
    samples=[]
    for row in records:
        if row.get('event')!='demonstration':continue
        state,action=row.get('features'),row.get('action')
        if (row.get('source')!='human' or row.get('stable') is not True or
                type(action) is not int or not 1<=action<=8 or not isinstance(state,list) or
                len(state)!=inputs or any(type(x) not in (int,float) or not math.isfinite(x) for x in state)):
            raise ValueError('Invalid human demonstration')
        samples.append({'state':state,'action':action})
    return samples


def optimize(model, optimizer, rows, demonstrations, coefficient):
    """Proximal policy optimization plus explicit behavior-cloning auxiliary loss."""
    model.requires_grad_(True).train()
    if not rows:
        model.critic.requires_grad_(False)
    if demonstrations:
        dx=torch.tensor([r['state'] for r in demonstrations],dtype=torch.float32)
        dy=torch.tensor([r['action'] for r in demonstrations],dtype=torch.long)
    if rows:
        states=torch.tensor([r['state'] for r in rows],dtype=torch.float32)
        masks=torch.tensor([r['mask'] for r in rows],dtype=torch.bool)
        actions=torch.tensor([r['action'] for r in rows])
        old=torch.tensor([r['log_probability'] for r in rows])
        with torch.no_grad():
            expected=Categorical(logits=model(states).masked_fill(~masks,-1e9)).log_prob(actions)
            if not torch.allclose(expected,old,atol=1e-4,rtol=1e-4):
                raise ValueError('Behavior probabilities do not match the selected on-policy checkpoint')
        advantage=advantages(rows)
        returns=advantage+torch.tensor([r['value'] for r in rows])
        if len(rows)>1:
            advantage=(advantage-advantage.mean())/advantage.std(unbiased=False).clamp_min(1e-6)
    before={k:v.detach().clone() for k,v in model.state_dict().items()}
    losses={}
    for _ in range(4):
        count=len(rows) if rows else len(demonstrations)
        for indices in torch.randperm(count).split(64):
            loss=torch.tensor(0.0)
            if rows:
                dist=Categorical(logits=model(states[indices]).masked_fill(~masks[indices],-1e9))
                ratio=(dist.log_prob(actions[indices])-old[indices]).exp()
                policy=-torch.minimum(ratio*advantage[indices],ratio.clamp(.8,1.2)*advantage[indices]).mean()
                critic=(value(model,states[indices])-returns[indices]).square().mean()
                loss=policy+.5*critic-.01*dist.entropy().mean()
                losses['reinforcement_loss']=float(policy.detach())
            if demonstrations:
                sample=torch.randperm(len(dx))[:64]
                cloning=F.cross_entropy(model(dx[sample]),dy[sample])
                loss=loss+(coefficient if rows else 1.0)*cloning
                losses['behavior_cloning_loss']=float(cloning.detach())
            if not torch.isfinite(loss):raise ValueError('Non-finite update; prior registry retained')
            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(),.5,error_if_nonfinite=True)
            optimizer.step()
    changed=sum(int(torch.count_nonzero(model.state_dict()[k]-v)) for k,v in before.items())
    return {**losses,'weights_changed':changed,'reinforcement_decisions':len(rows),
            'demonstration_samples':len(demonstrations)}


def maintain(home, session, coefficient=.1):
    """One post-session transaction; no model is overwritten or trained twice."""
    if not 0<coefficient<=1:raise ValueError('Hybrid behavior-cloning coefficient must be in (0,1]')
    home,session=Path(home).resolve(),Path(session).resolve()
    raw=session.read_bytes()
    if len(raw)>128*1024*1024:raise ValueError('Session exceeds bounded training input budget')
    digest=hashlib.sha256(raw).hexdigest()
    records=[json.loads(line) for line in raw.decode('utf-8').splitlines() if line.strip()]
    if (not records or records[0].get('event')!='session_start' or
            records[0].get('action_contract')!=CONTRACT or
            records[-1].get('event')!='session_end' or records[-1].get('status')!='completed'):
        raise ValueError('Expected a finalized client session, not partial or foreign data')
    automatic_human = records[0].get('collection') == 'automatic_human_dungeon'
    if automatic_human:
        end = records[-1]
        completion_proof(end)
        if (end.get('outcome') not in ('completed_all_bosses', 'server_dungeon_completed') or
                end.get('run') != records[0].get('run') or not end.get('run') or
                dungeon_identity(end) != dungeon_identity(records[0]) or end.get('instance') != records[0].get('instance') or
                any(r.get('event') in ('reinforcement_transition', 'policy_decision', 'track_selected') for r in records)):
            raise ValueError('Automatic human recording needs authoritative completion and no model experience')
    torch.set_num_threads(1)
    reports={}
    updates=[]
    with FileLock(str(home/'.training.lock'),timeout=0):
        paths=read_tracks(home/'models.json')
        registry=json.loads((home/'models.json').read_text())
        for track in ALGORITHMS:
            if automatic_human and track == 'rl':
                reports[track] = {'status':'explicit_reinforcement_only'}
                continue
            if track not in paths:continue
            model,metadata=load_frozen(paths[track])
            if metadata['algorithm']!=ALGORITHMS[track]:raise ValueError('Registry training method mismatch')
            with paths[track].open('rb') as stream:
                if hashlib.file_digest(stream,'sha256').hexdigest()!=metadata['sha256']:
                    raise ValueError('Checkpoint changed while loading')
                stream.seek(0)
                saved=torch.load(stream,map_location='cpu',weights_only=True)
            if automatic_human and tuple(saved.get('dungeon_identity', [389, 0])) != dungeon_identity(records[0]):
                raise ValueError('Dungeon/difficulty policy identity mismatch; use the separate matching store')
            if saved.get('action_contract')!=CONTRACT:raise ValueError('Foreign action contract in client store')
            if digest in saved.get('processed_sessions',[]):
                reports[track]={'status':'already_consumed'}
                continue
            fresh=human_samples(records,model.inputs) if track!='rl' else []
            rows=validate_transitions(records,track,metadata['sha256'],model.inputs) if track!='il' else []
            if not rows and not fresh:
                reports[track]={'status':'no_eligible_samples'}
                continue
            replay=(saved.get('demonstrations',[])+fresh)[-4096:] if track!='rl' else []
            model.requires_grad_(True)
            optimizer=torch.optim.Adam(model.parameters(),lr=.0001)
            if saved.get('optimizer'):optimizer.load_state_dict(saved['optimizer'])
            if saved.get('rng') is not None:torch.set_rng_state(saved['rng'])
            summary=optimize(model,optimizer,rows,replay,coefficient)
            version=int(saved['version'])+1
            path=home/track/f'v{version:06d}.pt'
            payload={**saved,'version':version,'model':model.state_dict(),'optimizer':optimizer.state_dict(),
                     'processed_sessions':[*saved.get('processed_sessions',[]),digest],
                     'demonstrations':replay,'parent_sha256':metadata['sha256'],
                     'last_session_sha256':digest,'hybrid_behavior_cloning_coefficient':coefficient,
                     'last_update':summary,'rng':torch.get_rng_state()}
            updates.append((path,payload))
            registry[track]=path.relative_to(home).as_posix()
            reports[track]={'status':'updated','version':version,**summary}
        # The sole commit point: all prior checkpoint pointers survive any earlier failure.
        published=[]
        try:
            for path,payload in updates:
                publish(path,payload)
                published.append(path)
            atomic_json(home/'models.json',registry)
        except BaseException:
            for path in published:
                if home in path.resolve().parents:path.unlink(missing_ok=True)
            raise
        try:
            atomic_json(home/'latest-training-summary.json',{'session_sha256':digest,'tracks':reports})
        except OSError:
            print('Training summary write failed; validated checkpoints remain published.')
    print('CLIENT TRAINING COMPLETE:')
    for track,report in reports.items():print(TRACK_NAMES[track]+': '+json.dumps(report))
    print('Independent source policies preserved; changed weights alone do not prove better dungeon performance.')
    from client_training_history import record_client_training
    record_client_training(home,digest,reports)
    return reports


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    init=sub.add_parser('initialize')
    init.add_argument('--home',required=True,type=Path)
    init.add_argument('--source',type=Path,help='Optional read-only common initialization checkpoint')
    init.add_argument('--inputs',type=int,default=26)
    init.add_argument('--seed',type=int,default=7)
    init.add_argument('--dungeon-map',type=int,default=389)
    init.add_argument('--difficulty',type=int,choices=(0,1),default=0)
    train=sub.add_parser('update')
    train.add_argument('--home',required=True,type=Path)
    train.add_argument('--session',required=True,type=Path)
    args=parser.parse_args()
    if args.command=='initialize':initialize(args.home,args.source,args.inputs,args.seed,args.dungeon_map,args.difficulty)
    else:maintain(args.home,args.session)


if __name__=='__main__':main()
