import unittest

from epochs import CellHistory, audit, build_scenario


class EpochTests(unittest.TestCase):
    def test_broad_read_does_not_delay_source_writes_or_independent_result(self):
        world = build_scenario(width=64)
        world.run(until=90_000)
        self.assertEqual(audit(world, 64), [])
        for process in world.actors.values():
            state = process.actor.history.logical_state()
            self.assertEqual(state["results"]["local-result"], {"unrelated": 7})
            self.assertNotIn("dependent", state["results"])
            self.assertEqual(state["versions"]["s63"][-1], [11, 163])
        self.assertTrue(world.explain("dependent")["waits"])
        world.run(until=200_000)
        self.assertEqual(audit(world, 64), [])
        self.assertFalse(world.explain("dependent")["waits"])

    def test_alternate_physical_schedules_reach_same_full_fixpoints(self):
        states = []
        for seed in range(8):
            world = build_scenario(seed=seed, ordering="shuffle")
            world.run(until=200_000)
            self.assertEqual(audit(world), [])
            states.extend(e["logical_hash"] for e in world.trace
                          if e["kind"] == "epoch_fixpoint" and e["epoch"] == 2)
        self.assertEqual(len(set(states)), 1)

    def test_negative_control_skipping_pending_effect_returns_stale_answer(self):
        world = build_scenario(ignore_pending=True)
        world.run(until=200_000)
        self.assertTrue(any("wrong snapshot" in error for error in audit(world)))

    def test_read_registers_bound_before_waiting(self):
        history = CellHistory({"x": 0})
        history.apply(dict(kind="announce", tx="old", effects=["x"], position=5))
        history.apply(dict(kind="read", tx="reader", scopes=["x"], position=10))
        self.assertEqual(history.fixpoint(), [])
        with self.assertRaisesRegex(ValueError, "earlier registered read"):
            history.apply(dict(kind="announce", tx="new", effects=["x"], position=9))
        history.apply(dict(kind="announce", tx="new", effects=["x"], position=11))
        self.assertEqual(history.blockers("reader"), ["old"])

    def test_installation_order_does_not_become_version_order(self):
        history = CellHistory({"x": 0})
        for i in range(1, 51):
            history.apply(dict(kind="announce", tx=str(i), effects=["x"], position=i))
        for i in reversed(range(1, 51)):
            history.apply(dict(kind="resolve", tx=str(i), values={"x": i}))
        history.apply(dict(kind="read", tx="reader", scopes=["x"], position=25))
        self.assertEqual(history.fixpoint(), [("reader", {"x": 25})])

    def test_repeated_transaction_reads_have_distinct_operation_identities(self):
        history = CellHistory({"x": 1, "y": 2})
        history.apply(dict(kind="read", tx="T", read_id="T/1", scopes=["x"], position=10))
        self.assertEqual(history.fixpoint(), [("T/1", {"x": 1})])
        history.apply(dict(kind="read", tx="T", read_id="T/2", scopes=["y"], position=10))
        self.assertEqual(history.fixpoint(), [("T/2", {"y": 2})])
        with self.assertRaisesRegex(ValueError, "identity reused"):
            history.apply(dict(kind="read", tx="T", read_id="T/1", scopes=["y"], position=10))

    def test_logical_state_includes_pending_continuation_inputs(self):
        states = []
        for scopes in (["x"], ["x", "y"]):
            history = CellHistory({"x": 0, "y": 2})
            history.apply(dict(kind="announce", tx="old", effects=["x"], position=5))
            history.apply(dict(kind="read", tx="broad", scopes=["x", "y"], position=10))
            history.apply(dict(kind="read", tx="pending", scopes=scopes, position=7))
            states.append(history.logical_state())
        self.assertNotEqual(states[0], states[1])

    def test_restart_reconstructs_from_retained_epochs(self):
        world = build_scenario()
        world.run(until=90_000)
        world.crash("consumer1")
        world.restart("consumer1")
        world.run(until=200_000)
        self.assertEqual(audit(world), [])
        states = [p.actor.history.logical_state() for p in world.actors.values()]
        self.assertEqual(states[0], states[1])
        self.assertEqual(states[1], states[2])


if __name__ == "__main__":
    unittest.main()
