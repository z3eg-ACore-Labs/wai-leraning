"""Dungeon routing and authoritative completion proof; never initializes a policy."""
from pathlib import Path


def identity(row):
    map_id, difficulty = row.get('map'), row.get('difficulty', 0)
    if type(map_id) is not int or map_id <= 0 or type(difficulty) is not int or not 0 <= difficulty <= 1:
        raise ValueError('Expected a dungeon map and normal/heroic difficulty')
    return map_id, difficulty


def policy_home(base, row):
    map_id, difficulty = identity(row)
    base = Path(base)
    # Preserve the existing normal Ragefire registry; do not migrate or copy weights.
    return base if (map_id, difficulty) == (389, 0) else base/'dungeons'/str(map_id)/str(difficulty)


def completion_proof(row):
    map_id, _ = identity(row)
    source = row.get('completion_source', 'ragefire_bosses' if map_id == 389 else None)
    if source == 'ragefire_bosses':
        if map_id != 389 or row.get('bosses') != 4:
            raise ValueError('Ragefire completion requires four observed unique bosses')
    elif source == 'encounter_mask':
        required, completed = row.get('required_mask'), row.get('completed_mask')
        if (type(required) is not int or type(completed) is not int or not 0 < required < 2**32 or
                not 0 <= completed < 2**32 or completed & required != required):
            raise ValueError('Server encounter completion requires every covered encounter')
    else:
        raise ValueError('Authoritative completion coverage is unavailable')
    return dict(completion_source=source, required_mask=row.get('required_mask', 0),
                completed_mask=row.get('completed_mask', 0), bosses=row.get('bosses', 0))
