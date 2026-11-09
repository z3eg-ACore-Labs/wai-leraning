"""Offline visit-memory behavior; no game server, model file or database is contacted."""
import unittest
from exploration import VisitMemory, INPUTS


def state(x=1, y=1, z=1):
    return [x / 1000, y / 1000, z / 1000, 0, 1, *([0] * 11)]


class ExplorationTests(unittest.TestCase):
    def test_start_and_stationary_positions_do_not_pay(self):
        memory = VisitMemory()
        self.assertEqual(memory.enter(state()), 0)
        self.assertEqual(memory.enter(state(), state()), 0)
        self.assertEqual(memory.summary()['entries'], 1)

    def test_boundary_pacing_pays_only_once(self):
        memory = VisitMemory()
        a, b = state(7), state(9)
        memory.enter(a)
        self.assertGreater(memory.enter(b, a), 0)
        for _ in range(20):
            self.assertEqual(memory.enter(a, b), 0)
            self.assertEqual(memory.enter(b, a), 0)
        self.assertAlmostEqual(memory.bonus_total, 0.02)

    def test_floors_negative_coordinates_and_failed_moves(self):
        memory = VisitMemory()
        self.assertNotEqual(memory.cell((1, 1, 1)), memory.cell((1, 1, 9)))
        self.assertNotEqual(memory.cell((-1, 1, 1)), memory.cell((1, 1, 1)))
        memory.enter(state(7))
        self.assertEqual(memory.enter(state(9), state(7), eligible=False), 0)

    def test_discontinuous_teleport_does_not_pay(self):
        memory = VisitMemory()
        memory.enter(state())
        self.assertEqual(memory.enter(state(100), state()), 0)

    def test_lifetime_counts_reduce_bonus_in_later_runs(self):
        counts = {}
        a, b = state(7), state(9)
        first, second = VisitMemory(counts), VisitMemory(counts)
        first.enter(a)
        bonus = first.enter(b, a)
        second.enter(a)
        self.assertLess(second.enter(b, a), bonus)

    def test_reward_budget_cannot_dominate_boss_rewards(self):
        memory = VisitMemory()
        previous = state()
        memory.enter(previous)
        for x in range(9, 2001, 8):
            following = state(x)
            memory.enter(following, previous)
            previous = following
        self.assertAlmostEqual(memory.bonus_total, 2)

    def test_input_features_change_after_a_visit(self):
        memory = VisitMemory()
        current = state(3, 3)
        memory.enter(current)
        before = memory.features(current)
        memory.enter(state(9, 3), current)
        after = memory.features(current)
        self.assertEqual(len(after), INPUTS)
        self.assertEqual(after[:16], current)
        self.assertLess(after[18], before[18])  # Forward endpoint is now visited.

    def test_neighbor_memory_rotates_with_server_heading(self):
        memory = VisitMemory()
        current = state(3, 3)
        memory.enter(current)
        memory.enter(state(9, 3), current)
        east = memory.features(current)
        north_state = list(current)
        north_state[3:5] = [1, 0]
        north = memory.features(north_state)
        self.assertAlmostEqual(east[18], north[24])  # East becomes right when facing north.


if __name__ == '__main__':
    unittest.main()
