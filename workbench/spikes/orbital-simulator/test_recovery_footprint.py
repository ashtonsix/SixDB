import unittest

from recovery_footprint import experiment


class RecoveryFootprintTests(unittest.TestCase):
    def test_same_completed_history_reopens_only_with_bounded_reads_at_tight_budget(self):
        whole, _ = experiment(12_000, "whole")
        stream, _ = experiment(12_000, "stream")
        self.assertEqual((whole["completed_before_reset"], stream["completed_before_reset"]), (12, 12))
        self.assertEqual(whole["resident_before_reset"], stream["resident_before_reset"])
        self.assertFalse(whole["shard_recovered"])
        self.assertEqual(whole["unfinished"], 1)
        self.assertTrue(stream["shard_recovered"])
        self.assertTrue(stream["recovered_state_matches"])
        self.assertEqual(stream["completed"], 13)
        self.assertFalse(whole["violations"] + stream["violations"])

    def test_extra_capacity_exposes_read_amortization_tradeoff(self):
        whole, _ = experiment(20_000, "whole", seed=7)
        stream, _ = experiment(20_000, "stream", seed=7)
        self.assertEqual((whole["completed"], stream["completed"]), (13, 13))
        self.assertLess(whole["recovery_ns"], stream["recovery_ns"])
        self.assertFalse(whole["violations"] + stream["violations"])

    def test_record_recovery_replays_exactly(self):
        row, world = experiment(12_000, "stream", seed=19)
        repeated, replay = experiment(12_000, "stream", seed=19, replay=world.decisions)
        replay.kernel.check_replay()
        self.assertEqual(row, repeated)
        self.assertEqual(world.report(), replay.report())


if __name__ == "__main__":
    unittest.main()
