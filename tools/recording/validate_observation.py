"""Validate observer JSONL order, pre-action pairing and known schema boundaries."""
import argparse
import collections
import json
import math
from pathlib import Path

OBSERVATION_VALIDATION_API = 1

KINDS = {'state', 'abilities', 'input', 'cast_pre_execute', 'cast_start', 'cast_go', 'cast_failed', 'logout',
         'episode_start', 'episode_end', 'observer_selection'}
FORBIDDEN = {'account', 'account_id', 'password', 'token', 'chat', 'character_name', 'guid', 'raw_packet'}


def check_value(value):
    if isinstance(value, dict):
        forbidden = FORBIDDEN.intersection(value)
        if forbidden:
            raise ValueError(f'Unexpected private/raw field: {sorted(forbidden)}')
        for child in value.values():
            check_value(child)
    elif isinstance(value, list):
        for child in value:
            check_value(child)
    elif isinstance(value, float) and not math.isfinite(value):
        raise ValueError('Non-finite numeric value')


def validate(records):
    counts = collections.Counter()
    requests = {}
    previous = 0
    previous_time = -1
    missing = 0
    unmatched = 0
    summary = None
    active_episode = None
    episodes = set()
    human_actor = None
    for record in records:
        check_value(record)
        if record.get('schema') != 1:
            raise ValueError('Unsupported schema')
        kind = record.get('kind')
        if summary is not None:
            raise ValueError('Record after writer summary')
        if kind == 'writer_summary':
            summary = record
            if not isinstance(record.get('dropped'), int) or record['dropped'] < 0:
                raise ValueError('Invalid dropped count')
            continue
        if kind not in KINDS:
            raise ValueError('Unknown record kind')
        seq, time = record.get('seq'), record.get('time_ms')
        if not isinstance(seq, int) or seq <= previous:
            raise ValueError('Sequence is not strictly increasing')
        if not isinstance(time, int) or time < previous_time:
            raise ValueError('Clock moved backward')
        missing += seq - previous - 1
        previous, previous_time = seq, time
        counts[kind] += 1
        if kind == 'observer_selection':
            if active_episode is not None:
                raise ValueError('Observer changed inside an open episode')
            status, actor = record.get('status'), record.get('actor')
            if status not in {'active_human', 'no_human', 'multiple_humans'}:
                raise ValueError('Unknown observer selection status')
            if not isinstance(actor, int) or actor < 0 or (status == 'active_human') != (actor > 0):
                raise ValueError('Observer selection lacks an unambiguous actor')
            human_actor = actor
        elif kind == 'episode_start':
            episode = record.get('episode')
            if active_episode is not None or not isinstance(episode, int) or episode <= 0 or episode in episodes:
                raise ValueError('Invalid or overlapping episode start')
            active_episode = episode
            episodes.add(episode)
        elif kind == 'episode_end':
            if record.get('episode') != active_episode or active_episode is None:
                raise ValueError('Episode end without matching start')
            active_episode = None
        elif 'episode' in record and kind != 'logout' and record['episode'] != active_episode:
            raise ValueError('Record outside its episode')
        if kind in {'input', 'state'}:
            state = record.get('state')
            if not isinstance(state, dict) or not isinstance(state.get('self'), dict):
                raise ValueError('Missing actor state')
            if human_actor is not None and (human_actor == 0 or state['self'].get('actor') != human_actor):
                raise ValueError('State recorded for an unselected human')
            if len(state.get('party', [])) > 40:
                raise ValueError('Party cap exceeded')
            for actor in [state['self'], *state.get('party', []), state.get('selected')]:
                if actor is None:
                    continue
                if not isinstance(actor.get('actor'), int) or actor['actor'] < 0:
                    raise ValueError('Invalid session-local actor identifier')
                if len(actor.get('position', [])) != 4:
                    raise ValueError('Missing position/facing')
                if len(actor.get('auras', [])) > 32:
                    raise ValueError('Aura cap exceeded')
        if kind == 'input':
            if record.get('state_phase') != 'pre_handler' or record.get('source') != 'human_request':
                raise ValueError('Input lacks explicit pre-handler provenance')
            if 'spell' in record and not record.get('decode_error'):
                requests[seq] = record
        if kind.startswith('cast_'):
            reference = record.get('request_seq', 0)
            if reference >= seq:
                raise ValueError('Cast references future state/request')
            request = requests.get(reference)
            if not request:
                unmatched += 1
            elif request.get('spell') != record.get('spell') or request.get('cast_count') != record.get('cast_count'):
                raise ValueError('Cast/request correlation mismatch')
    if summary and summary['dropped'] < missing:
        raise ValueError('Sequence gaps exceed reported record losses')
    return {'counts': dict(counts), 'missing_sequences': missing, 'unmatched_cast_events': unmatched,
            'clean_shutdown': summary is not None, 'dropped': summary['dropped'] if summary else None,
            'episodes': len(episodes), 'incomplete_episode': active_episode is not None,
            'training_ready': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('events', type=Path)
    args = parser.parse_args()
    with args.events.open(encoding='utf-8') as stream:
        result = validate(json.loads(line) for line in stream if line.strip())
    print(json.dumps(result, indent=2))
