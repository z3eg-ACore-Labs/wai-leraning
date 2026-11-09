"""Convert selected-human observer telemetry into bounded movement demonstrations."""
import hashlib
import json
import math
from pathlib import Path

from navigation_pairs import build_pairs


def navigation_point(record):
    if not isinstance(record, dict) or type(record.get('schema')) is not int or record['schema'] != 1:
        return None
    if record.get('kind') != 'state' or not isinstance(record.get('state'), dict):
        return None
    state = record['state']
    actor = state.get('self')
    if not isinstance(actor, dict) or actor.get('alive') is not True or actor.get('combat') is not False:
        return None
    for value in (state.get('map'), state.get('instance'), record.get('episode'), record.get('seq')):
        if type(value) is not int or value <= 0:
            return None
    if type(record.get('time_ms')) is not int or record['time_ms'] < 0:
        return None
    if 'casting' not in actor and 'casts' not in actor:
        return None
    if 'casting' in actor and actor['casting'] is not False:
        return None
    if 'casts' in actor and (not isinstance(actor['casts'], list) or actor['casts']):
        return None
    position = actor.get('position')
    if not isinstance(position, list) or len(position) != 4:
        return None
    copied = []
    for value in position:
        if type(value) not in (int, float):
            return None
        try:
            number = float(value)
        except (OverflowError, ValueError):
            return None
        if not math.isfinite(number):
            return None
        copied.append(number)
    return dict(map=state['map'], instance=state['instance'], episode=record['episode'],
                seq=record['seq'], time_ms=record['time_ms'], position=copied)


def read_demonstration(path, map_id, difficulty=0):
    """One closed visit, no mixing runs, episodes, combat gaps, teleports or model output."""
    path = Path(path)
    if path.stat().st_size > 128 * 1024 * 1024:
        raise ValueError('Recording exceeds the bounded importer limit')
    rows = []
    segment = []

    def flush():
        rows.extend(build_pairs(segment))
        segment.clear()

    previous = None
    active = None
    closed = False
    summary = False
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        record = json.loads(line)
        if not isinstance(record, dict):
            raise ValueError('Expected observer objects')
        kind = record.get('kind')
        if kind == 'episode_start':
            if active is not None or record.get('map') != map_id or record.get('difficulty', 0) != difficulty:
                raise ValueError('Recording scope differs from requested dungeon/difficulty')
            if record.get('provenance', 'human') != 'human':
                raise ValueError('Automated behavior is not a human demonstration')
            active = record.get('episode')
            previous = None
        elif kind == 'episode_end':
            if active is None or record.get('episode') != active:
                raise ValueError('Invalid recording boundary')
            closed = True
            flush()
            previous = None
        elif kind == 'writer_summary':
            if type(record.get('dropped')) is not int or record['dropped'] != 0:
                raise ValueError('Recording lost events; cannot certify movement continuity')
            summary = True
        elif kind == 'state' and active is not None and not closed:
            point = navigation_point(record)
            if point is None or point['episode'] != active or point['map'] != map_id:
                flush()
                previous = None
                continue
            continuous = False
            if previous and point['instance'] == previous['instance'] and point['seq'] > previous['seq']:
                seconds = (point['time_ms'] - previous['time_ms']) / 1000
                a, b = previous['position'], point['position']
                distance = math.dist(a[:3], b[:3])
                continuous = (0.05 <= seconds <= 1 and distance <= min(4, 10 * seconds) and abs(b[2] - a[2]) <= 2)
            if not continuous:
                flush()
            segment.append(point)
            previous = point
    if not closed or not summary:
        raise ValueError('Recording is still open; wait for the visit or recording segment to close')
    if len(rows) < 64:
        raise ValueError('Need at least 64 valid human movement pairs')
    # Hash for provenance without exporting character identities or source paths.
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return rows, digest
