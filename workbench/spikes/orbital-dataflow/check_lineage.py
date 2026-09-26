import unittest

from lineage_probe import Binding, Job, Unavailable, study


def oracle(binding):
    values = list(range(binding.snapshot * 1000, binding.snapshot * 1000 + 128))
    for _ in range(binding.iterations):
        for index in range(len(values)):
            values[index] = (values[index] * (2 * binding.code + 1) + binding.code) % 1000003
    return {"count": 128, "sum": sum(values), "top3": sorted(values)[-3:][::-1]}


class LineageChecks(unittest.TestCase):
    def test_lazy_creation(self):
        job = Job()
        self.assertEqual(job.calls, 0)
        self.assertEqual(job.source_reads, 0)

    def test_results_across_cache_sizes_and_bindings(self):
        for snapshot in (1, 2):
            for code in (1, 2):
                for iterations in (1, 3, 20):
                    for cache in (0, 255, 256, 512, 1024):
                        binding = Binding(snapshot, code, iterations)
                        job = Job(binding, cache_bytes=cache)
                        self.assertEqual(job.action(), oracle(binding))
                        self.assertEqual(job.action(), oracle(binding))
                        self.assertLessEqual(job.peak_cache, cache)

    def test_reuse_and_capacity_counterexample(self):
        results = {case["name"]: case for case in study()}
        self.assertEqual(results["no-cache"]["extension_record_calls"], 256)
        self.assertEqual(results["fitting-cache"]["extension_record_calls"], 128)
        self.assertEqual(results["thrashing-cache"]["extension_record_calls"], 256)

    def test_partition_reconstruction_is_exact(self):
        for checkpoint in (0, 512, 1024):
            job = Job(Binding(1, 1, 20), checkpoint_bytes=checkpoint, checkpoint_iteration=10)
            first = job.action()
            job.lose_cache()
            self.assertEqual(job.action(), first)
            self.assertLessEqual(len(job.checkpoints) * 256, checkpoint)

    def test_checkpoint_shortens_selected_recovery(self):
        results = {case["name"]: case for case in study()}
        self.assertEqual(results["lost-iteration-lineage"]["recovery_record_calls"], 640)
        self.assertEqual(results["lost-iteration-checkpoint"]["recovery_record_calls"], 320)
        self.assertEqual(results["partial-checkpoint"]["recovery_record_calls"], 640)
        self.assertEqual(results["lost-partition-checkpoint"]["recovery_record_calls"], 0)

    def test_missing_capture_is_not_latest_alias(self):
        job = Job()
        job.action()
        job.lose_cache(0)
        job.sources.remove(1)
        with self.assertRaises(Unavailable):
            job.action()
        job.sources.add(1)
        job.codes.remove(1)
        with self.assertRaises(Unavailable):
            job.action()

    def test_full_checkpoint_can_supply_completed_representation(self):
        job = Job(checkpoint_bytes=1024, checkpoint_iteration=1)
        original = job.action()
        job.sources.clear()
        job.codes.clear()
        job.lose_cache()
        self.assertEqual(job.action(), original)
        # No claim that output bytes replace required execution/check evidence.

    def test_snapshot_and_code_are_in_reuse_identity(self):
        job = Job()
        original = job.action()
        job.binding = Binding(2, 2)
        self.assertEqual(job.action(), oracle(job.binding))
        self.assertNotEqual(original, job.action())
        self.assertEqual(job.calls, 256)
        bad = next(case for case in study() if case["name"] == "unsafe-live-alias-recovery")
        self.assertTrue(bad["mismatch"])

    def test_outside_captured_coverage_rejected(self):
        with self.assertRaises(ValueError):
            Job().partition(4)


if __name__ == "__main__":
    unittest.main()
