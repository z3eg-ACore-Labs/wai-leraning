"""Position-derived visit memory. No boss coordinates, geometry map, or prescribed route."""
import math

RAW_INPUTS = 16
INPUTS = 26
SETTINGS = {'version': 1, 'cell_xy_yards': 8, 'cell_z_yards': 4,
            'new_cell_bonus': 0.02, 'episode_bonus_cap': 2.0}
DIRECTIONS = ((1, 0), (.707107, .707107), (0, 1), (-.707107, .707107),
              (-1, 0), (-.707107, -.707107), (0, -1), (.707107, -.707107))


class VisitMemory:
    def __init__(self, lifetime=None):
        self.lifetime = lifetime if lifetime is not None else {}
        self.visits = {}
        self.last = None
        self.bonus_total = 0.0

    @staticmethod
    def position(features):
        if len(features) != RAW_INPUTS or any(not math.isfinite(x) for x in features):
            raise ValueError('Invalid server position features')
        return tuple(x * 1000 for x in features[:3])

    @staticmethod
    def cell(position):
        return tuple(math.floor(x / size) for x, size in zip(position, (8, 8, 4)))

    @staticmethod
    def key(cell):
        return ','.join(map(str, cell))

    def enter(self, features, previous=None, eligible=True):
        position = self.position(features)
        cell = self.cell(position)
        if cell == self.last:
            return 0.0
        self.last = cell
        first = cell not in self.visits
        self.visits[cell] = self.visits.get(cell, 0) + 1
        key = self.key(cell)
        frequency = self.lifetime.get(key, 0)
        self.lifetime[key] = frequency + 1
        if not first or previous is None or not eligible:
            return 0.0
        old = self.position(previous)
        distance = math.dist(old, position)
        # Discontinuities such as recovery summons are recorded but earn no exploration credit.
        if not 1 <= distance <= 12 or abs(position[2] - old[2]) > 6:
            return 0.0
        bonus = min(SETTINGS['new_cell_bonus'] / math.sqrt(1 + frequency),
                    SETTINGS['episode_bonus_cap'] - self.bonus_total)
        self.bonus_total += max(0, bonus)
        return max(0, bonus)

    def features(self, raw):
        position = self.position(raw)
        cell = self.cell(position)
        sine, cosine = raw[3], raw[4]
        neighbors = []
        for forward, left in DIRECTIONS:
            endpoint = (position[0] + 6 * (forward * cosine - left * sine),
                        position[1] + 6 * (forward * sine + left * cosine), position[2])
            visits = self.visits.get(self.cell(endpoint), 0)
            neighbors.append(1 / math.sqrt(1 + visits))
        return list(raw) + [1 - 1 / math.sqrt(1 + self.visits.get(cell, 0)),
                            min(len(self.visits) / 1000, 1), *neighbors]

    def summary(self):
        return {'unique_cells': len(self.visits), 'entries': sum(self.visits.values()),
                'bonus': self.bonus_total, 'cell_size_yards': [8, 8, 4]}
