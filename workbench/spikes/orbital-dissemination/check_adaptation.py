"""Structural, causal and accounting checks for the bounded routing studies."""
import unittest

from adaptation_study import choice, network, plan_family, run_routes, run_senders
from simulator import Sim


class AdaptationChecks(unittest.TestCase):
    def accounting(self, row):
        self.assertEqual(row["offered"], row["completed"] + row["unfinished"])
        self.assertEqual(row["offered"], row["on_time"] + row["late_completed"] + row["unfinished"])
        self.assertLessEqual(row["accepted_recipient_pairs"], row["required_recipient_pairs"])
        self.assertGreater(row["cpu_service_us"], 0)
        self.assertGreater(row["counters"]["wire_bytes"], 0)

    def test_constrained_family_reaches_all_without_cycles_or_hidden_crossings(self):
        recipients = tuple(f"r{i}" for i in range(16))
        net = network(Sim(1), dict(source="a", **dict.fromkeys(recipients, "b")), {})
        family = plan_family(net, recipients, 256, "shallow_hash")
        self.assertEqual(len(family), 4)
        gateways = set()
        for plan in family:
            children = {}
            for edge in plan:
                children.setdefault(edge.src, []).append(edge.dst)
            depth = {"source": 0}
            todo = ["source"]
            while todo:
                parent = todo.pop()
                for child in children.get(parent, ()):
                    self.assertNotIn(child, depth)
                    depth[child] = depth[parent] + 1
                    todo.append(child)
            self.assertEqual(set(depth) - {"source"}, set(recipients))
            self.assertLessEqual(max(depth.values()), 3)
            self.assertLessEqual(max(map(len, children.values())), 4)
            remote = [e for e in plan if net.nodes[e.src].zone != net.nodes[e.dst].zone]
            self.assertEqual(len(remote), 1)
            gateways.add(remote[0].dst)
        self.assertEqual(len(gateways), 4)

    def test_route_choice_is_replayable_and_stripes_bound_short_window_imbalance(self):
        for key in range(100):
            self.assertEqual(choice(key, 4, "shallow_hash", 19),
                             choice(key, 4, "shallow_hash", 19))
        self.assertEqual([choice(key, 4, "shallow_stripes") for key in range(8)],
                         [0, 1, 2, 3, 0, 1, 2, 3])
        self.assertEqual({choice(key, 4, "shallow_block_stripes") for key in range(32)}, {0})

    def test_equivalent_bytes_expose_gateway_burst_cost(self):
        cfg = dict(count=160, rate=100000, size=4096)
        stripes = run_routes(dict(cfg, policy="shallow_stripes"))
        blocks = run_routes(dict(cfg, policy="shallow_block_stripes"))
        cheap = run_routes(dict(cfg, policy="fixed_cheap"))
        for row in (stripes, blocks, cheap):
            self.accounting(row)
        self.assertEqual(stripes["completed"], 160)
        self.assertEqual(blocks["completed"], 160)
        self.assertAlmostEqual(stripes["counters"]["wire_bytes"], blocks["counters"]["wire_bytes"], places=5)
        self.assertGreater(blocks["resource_peak_bytes"], stripes["resource_peak_bytes"])
        self.assertGreater(blocks["p99_us"], stripes["p99_us"])
        self.assertGreater(cheap["unfinished"], 0)
        self.assertGreater(cheap["overflow_events"], 0)

    def test_sender_suppression_waits_for_actual_notice(self):
        row = run_senders(dict(count=1, policy="eager"))
        self.accounting(row)
        self.assertEqual(row["completed"], 1)
        self.assertEqual(row["counters"]["logical_recipient_effects"], 1)
        # s0 sent before s1's faster-path result existed. Its in-flight copy
        # cannot disappear when the receiver accepts s1; s2 can learn in time.
        self.assertEqual(row["counters"]["duplicate_equivalent_deliveries"], 1)
        self.assertEqual(row["suppressed_by_origin"], {"s2": 1})
        self.assertEqual(row["messages_with_multiple_senders"], 1)

    def test_origin_crash_needs_backup_and_does_not_reexecute_targets(self):
        cfg = dict(count=60, receivers=3, faults=[dict(kind="crash", node="s1", at_us=0)])
        single = run_senders(dict(cfg, policy="single_ranked"))
        timed = run_senders(dict(cfg, policy="timed_ranked"))
        eager = run_senders(dict(cfg, policy="eager"))
        for row in (single, timed, eager):
            self.accounting(row)
            self.assertEqual(row["counters"]["logical_recipient_effects"],
                             row["accepted_recipient_pairs"])
        self.assertGreater(single["unfinished"], 0)
        self.assertEqual(timed["completed"], 60)
        self.assertEqual(eager["completed"], 60)
        self.assertEqual(eager["accepted_recipient_pairs"], 180)
        self.assertLess(eager["p99_us"], timed["p99_us"])

    def test_lost_receipts_cost_work_not_additional_effects(self):
        cfg = dict(count=30, policy="eager", receivers=3)
        healthy = run_senders(cfg)
        lost = run_senders(dict(cfg, receipt_loss=.5))
        self.assertEqual(healthy["completed"], lost["completed"])
        self.assertEqual(lost["counters"]["logical_recipient_effects"], 90)
        self.assertGreater(lost["counters"]["wire_bytes"], healthy["counters"]["wire_bytes"])
        self.assertGreater(lost["counters"]["retry_messages"], 0)

    def test_sources_and_message_choices_replay(self):
        cfg = dict(count=20, policy="shallow_hash", seed=73)
        self.assertEqual(run_routes(cfg), run_routes(cfg))
        cfg = dict(count=20, policy="timed_ranked", receipt_loss=.2, seed=73)
        self.assertEqual(run_senders(cfg), run_senders(cfg))


if __name__ == "__main__":
    unittest.main()
