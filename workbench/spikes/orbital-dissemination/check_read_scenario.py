import unittest

from read_scenario import run_reads


class Reads(unittest.TestCase):
    def test_partition_work_is_constant(self):
        for pieces in (1, 4, 7):
            r = run_reads(dict(count=3, rate=100, shards=3, pieces=pieces,
                               bytes_per_shard=100003))
            self.assertEqual(r["completed"], 3)
            self.assertEqual(r["scan_bytes"], 900027)

    def test_hedge_charges_executed_work(self):
        r = run_reads(dict(count=3, rate=100, shards=1, pieces=1, hedge_us=0))
        self.assertEqual(r["completed"], 3)
        self.assertEqual(r["scan_bytes"], 2 * r["logical_scan_bytes"])

    def test_missing_shard_does_not_complete(self):
        r = run_reads(dict(count=2, shards=2, replicas=1, pieces=1,
                           faults=[dict(kind="crash", node="w1_0", at_us=0)]))
        self.assertEqual(r["completed"], 0)
        self.assertEqual(r["unfinished"], 2)


if __name__ == "__main__":
    unittest.main()
