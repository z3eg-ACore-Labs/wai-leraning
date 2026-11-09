"""Client-only reward transitions. No use of server-action or other-policy trajectories."""
import math

import torch

from client_demonstrations import CONTRACT


def counters(observation):
    if not observation.get('boss_progress_available') or not observation.get('episode_time_available'):
        raise ValueError('Authoritative client reward telemetry is unavailable; rebuild/enable its server module')
    result={k:observation.get(k) for k in ('bosses','deaths','elapsed_ms')}
    if any(type(x) is not int or x<0 for x in result.values()) or result['bosses']>4:
        raise ValueError('Invalid client reward counters')
    result['outcome']=observation.get('outcome')
    if result['outcome'] not in ('running','player_died','completed_all_bosses'):
        raise ValueError('Invalid client outcome')
    if result['outcome']=='completed_all_bosses' and result['bosses']!=4:
        raise ValueError('Completion requires all four bosses')
    return result


def estimate(model, features):
    with torch.inference_mode():
        result=float(model.critic(model.encoder(torch.tensor(features,dtype=torch.float32))).squeeze())
    if not math.isfinite(result):raise ValueError('Non-finite critic estimate')
    return result


class ClientRollout:
    def __init__(self,emit):
        self.emit=emit
        self.pending=None
        self.segment=1

    def truncate(self):
        # Earlier observed transitions carry their own next-state bootstrap.
        # The still-pending action has no validated outcome and is not trained.
        self.pending=None
        self.segment+=1

    def start(self,model,features,raw,obs,track,metadata,action,probabilities):
        if track=='il':return
        before=counters(obs)
        mask=[True]+[bool(obs['ready'])]*8
        self.pending={'model':model,'state':list(features),'raw':list(raw),'before':before,
                      'track':track,'checkpoint_sha256':metadata['sha256'],
                      'mask':mask,'action':action,'log_probability':math.log(max(probabilities[action],1e-30)),
                      'value':estimate(model,features),'sequence':obs['seq'],'segment':self.segment}

    def observe(self,obs,features,bonus=0):
        if not self.pending:return
        pending,self.pending=self.pending,None
        before,after=pending['before'],counters(obs)
        if any(after[k]<before[k] for k in ('bosses','deaths','elapsed_ms')):
            raise ValueError('Client reward counters moved backwards')
        done=after['outcome']!='running'
        reward=10*(after['bosses']-before['bosses'])-5*(after['deaths']-before['deaths'])
        reward-=.01*(after['elapsed_ms']-before['elapsed_ms'])/1000
        if done:reward+=1000 if after['outcome']=='completed_all_bosses' else -50
        if after['deaths']!=before['deaths']:bonus=0
        self.emit('reinforcement_transition',action_contract=CONTRACT,
                  **{k:v for k,v in pending.items() if k not in ('model','raw','before')},
                  next_state=list(features),next_value=0 if done else estimate(pending['model'],features),
                  reward=reward+bonus,environment_reward=reward,exploration_bonus=bonus,done=done)
        if done:self.segment+=1
