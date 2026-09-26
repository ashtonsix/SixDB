"""Independent finite-capacity expectations and lifecycle checks."""
from dataclasses import replace
import random
import unittest

from resource_probe import Case, cases, run


class ResourceChecks(unittest.TestCase):
    def test_all_selected_cases_preserve_capacity_and_exact_result(self):
        for case in cases():
            with self.subTest(case=case.name):
                result = run(case)
                self.assertLessEqual(result["peak_memory_bytes"], case.memory_bytes)
                self.assertLessEqual(result["peak_spool_bytes"], case.spool_bytes)
                self.assertEqual(result["consumed_chunks"] + result["unfinished_chunks"], result["offered_chunks"])
                if result["published"]:
                    expected = sum(value for value in range(1, case.inputs + 1)
                                   for _ in range(case.output_bytes_per_input))
                    self.assertEqual(result["sum"], expected)
                    self.assertEqual(result["unfinished_chunks"], 0)
                    self.assertEqual(result["outstanding_memory_bytes"], 0)
                    self.assertEqual(result["outstanding_spool_bytes"], 0)
                    self.assertEqual(result["outstanding_source_pin_bytes"], 0)

    def test_acyclic_retention_deadlock(self):
        stalled = run(Case("ram", "ram"))
        self.assertEqual(stalled["status"], "stalled")
        self.assertEqual(stalled["halt_reason"], "resource_wait")
        self.assertEqual(stalled["consumed_chunks"], 7)
        self.assertEqual(stalled["outstanding_memory_bytes"], 896)
        self.assertIsNone(stalled["retired_at"])
        self.assertEqual(stalled["outstanding_source_pin_bytes"], 192)

    def test_partitioned_budget_is_not_new_capacity(self):
        result = run(Case("split", "split"))
        self.assertEqual(result["status"], "stalled")
        self.assertEqual(result["consumed_chunks"], 6)

    def test_materialization_needs_full_retention_space(self):
        for storage, expected in ((512, "stalled"), (1536, "published")):
            result = run(Case("spool", "spool", spool_bytes=storage))
            self.assertEqual(result["status"], expected)
        full = run(Case("spool", "spool"))
        self.assertEqual(full["spool_write_bytes"], 1536)

    def test_small_work_needs_no_spool(self):
        base = Case("small", "ram", inputs=4)
        ram = run(base)
        spool = run(replace(base, policy="spool"))
        self.assertEqual(ram["status"], "published")
        self.assertLess(ram["publication_at"], spool["publication_at"])

    def test_replay_is_a_declared_semantic_option(self):
        self.assertEqual(run(Case("bad", "replay", source_replayable=False))["status"], "rejected")
        result = run(Case("replay", "replay"))
        self.assertEqual(result["status"], "published")
        self.assertEqual(result["spool_write_bytes"], 0)
        self.assertEqual(result["peak_source_pin_bytes"], 192)

    def test_chunking_makes_expansion_runnable(self):
        base = Case("expansion", "replay", output_bytes_per_input=512,
                    grain_bytes=512, memory_bytes=256)
        self.assertEqual(run(base)["consumed_chunks"], 0)
        chunked = run(replace(base, grain_bytes=32))
        self.assertEqual(chunked["status"], "published")
        self.assertEqual(chunked["sum"], 39936)
        self.assertEqual(chunked["source_read_bytes"], 3072)

    def test_full_reservation_rejects_before_obligations(self):
        result = run(Case("admit", "admit"))
        self.assertEqual(result["status"], "rejected")
        self.assertEqual(result["cpu_ticks"], 0)
        self.assertEqual(result["peak_source_pin_bytes"], 0)

    def test_cancel_waits_for_actual_stopping(self):
        base = Case("cancel", "replay", cancel_at=5, resolution_delay=2)
        large = run(base)
        small = run(replace(base, grain_bytes=16))
        self.assertEqual(large["retired_at"], 17)
        self.assertEqual(small["retired_at"], 7)
        self.assertFalse(large["published"])
        self.assertEqual(large["consumed_chunks"], 0)
        self.assertGreater(small["consumed_chunks"], 0)

    def test_cancel_waits_for_io_and_resolution(self):
        slow_io = run(Case("io", "spool", io_ticks_per_byte=1,
                           cancel_at=20, resolution_delay=2))
        self.assertEqual(slow_io["retired_at"], 147)
        self.assertEqual(slow_io["spool_write_bytes"], 128)
        slow_resolution = run(Case("resolution", "ram", cancel_at=20, resolution_delay=1000))
        self.assertEqual(slow_resolution["retired_at"], 1020)

    def test_cancellation_can_resolve_resource_stall(self):
        result = run(Case("cancel-stall", "ram", cancel_at=500, resolution_delay=2))
        self.assertEqual(result["status"], "cancelled")
        self.assertEqual(result["retired_at"], 502)
        self.assertEqual(result["consumed_chunks"], 7)

    def test_publication_delay_charges_byte_lifetime(self):
        short = run(Case("short", "ram", inputs=4))
        long = run(Case("long", "ram", inputs=4, publication_delay=1010))
        self.assertEqual(long["memory_byte_ticks"] - short["memory_byte_ticks"], 512000)
        self.assertEqual(long["source_byte_ticks"] - short["source_byte_ticks"], 64000)

    def test_random_capacities_against_closed_form(self):
        randomizer = random.Random(1926)
        for _ in range(200):
            n = randomizer.randint(1, 9)
            output = randomizer.randint(1, 60)
            grain = randomizer.randint(1, output)
            memory = randomizer.randint(1, 700)
            source = 16 * n
            base = Case("random", "ram", inputs=n, output_bytes_per_input=output,
                        grain_bytes=grain, memory_bytes=memory, source_bytes=source)
            self.assertEqual(run(base)["published"], n * output + 16 <= memory)
            self.assertEqual(run(replace(base, policy="replay"))["published"], grain + 16 <= memory)
            self.assertEqual(run(replace(base, policy="spool", spool_bytes=n * output))["published"],
                             grain + 16 <= memory)

    def test_horizon_counts_only_executed_cpu(self):
        result = run(Case("short-drain", "replay", horizon=1))
        self.assertEqual(result["halt_reason"], "horizon")
        self.assertEqual(result["cpu_ticks"], 1)
        self.assertEqual(result["cpu_scheduled_ticks"], 17)
        self.assertEqual(result["consumed_chunks"], 0)
        self.assertEqual(result["memory_byte_ticks"], 144)

    def test_time_cannot_run_backward_or_be_nonfinite(self):
        for field in ("compute_ticks_per_byte", "task_ticks", "io_ticks_per_byte",
                      "io_fixed_ticks", "publication_delay", "resolution_delay", "horizon", "cancel_at"):
            for invalid in (-1, float("nan"), float("inf")):
                with self.assertRaises(ValueError):
                    run(replace(Case("invalid", "ram"), **{field: invalid}))


if __name__ == "__main__":
    unittest.main()
