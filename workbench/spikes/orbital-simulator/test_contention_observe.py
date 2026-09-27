import unittest

from traffic import Shard, Strategy


class ObservationTests(unittest.TestCase):
    def test_private_observation_retains_pending_gates_without_changing_state(self):
        shard = Shard(0, {"s0:k": 10, "s0:other": 20}, Strategy())
        for kind, fields in (("acquire", dict(locks=["s0:k"], writes=["s0:k"])),
                             ("announce", {}), ("fix", dict(position=100))):
            shard.apply(dict(request=f"1/{kind}", kind=kind, tx=1, reply="caller", **fields))
        before = shard.logical_state()
        self.assertEqual(shard.observe_at(2, 99, ["s0:k"]), {"values": {"s0:k": 10}})
        self.assertEqual(shard.observe_at(2, 100, ["s0:k"]), {"pending": [1]})
        self.assertEqual(shard.observe_at(2, 100, ["s0:other"]), {"values": {"s0:other": 20}})
        self.assertEqual(shard.logical_state(), before)
        shard.apply(dict(request="1/resolve", kind="resolve", tx=1, reply="caller",
                         values={"s0:k": 11}))
        before = shard.logical_state()
        self.assertEqual(shard.observe_at(2, 99, ["s0:k"]), {"values": {"s0:k": 10}})
        self.assertEqual(shard.observe_at(2, 100, ["s0:k"]), {"values": {"s0:k": 11}})
        self.assertEqual(shard.logical_state(), before)

    def test_replacement_only_skips_dependencies_covered_at_the_requested_cut(self):
        shard = Shard(0, {"s0:a": 10, "s0:b": 20}, Strategy(supersede=True))
        for tx, keys, position in ((1, ["s0:a", "s0:b"], 100), (2, ["s0:a"], 200)):
            for kind, fields in (("acquire", dict(locks=keys, writes=keys)),
                                 ("announce", {}), ("fix", dict(position=position)),
                                 ("release", {})):
                shard.apply(dict(request=f"{tx}/{kind}", kind=kind, tx=tx, reply="caller", **fields))
        shard.apply(dict(request="2/resolve", kind="resolve", tx=2, reply="caller",
                         values={"s0:a": 30}))
        before = shard.logical_state()
        self.assertEqual(shard.observe_at(3, 200, ["s0:a"]), {"values": {"s0:a": 30}})
        self.assertEqual(shard.observe_at(3, 150, ["s0:a"]), {"pending": [1]})
        self.assertEqual(shard.observe_at(3, 200, ["s0:a", "s0:b"]), {"pending": [1]})
        self.assertEqual(shard.logical_state(), before)


if __name__ == "__main__":
    unittest.main()
