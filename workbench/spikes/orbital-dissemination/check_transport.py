#!/usr/bin/env python3
"""Finite-resource and crash counterexamples independent of workload callbacks."""
import unittest

from simulator import Edge, Network, Node, Resource, Sim, resource_report


def quiet_node(name, rate=1000, **kwargs):
    return Node(name, cpu_packet_us=0, cpu_byte_us=0, cpu_slots=16,
                nic_bytes_us=rate, queue_bytes=1 << 25, **kwargs)


class TransportCases(unittest.TestCase):
    def test_direct_packet_serialization_overlaps_tx_and_rx(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a"), quiet_node("b")],
                      {("a", "b"): Edge(10)}, mtu=1000100)
        arrivals = []
        net.send("a", "b", 1000000, lambda: arrivals.append(sim.now), reliable=False)
        sim.run(2000)
        self.assertEqual(len(arrivals), 1)
        self.assertAlmostEqual(arrivals[0], 1010.096)
        self.assertAlmostEqual(net.tx["a"].busy_us, 1000.096)
        self.assertAlmostEqual(net.rx["b"].busy_us, 1000.096)

    def test_incast_shares_receiver_nic(self):
        sim = Sim()
        nodes = [quiet_node(str(i)) for i in range(9)]
        net = Network(sim, nodes, {(str(i), "8"): Edge(1) for i in range(8)}, mtu=1000100)
        arrivals = []
        for i in range(8):
            net.send(str(i), "8", 1000000, lambda: arrivals.append(sim.now), reliable=False)
        sim.run(1100)
        self.assertEqual(len(arrivals), 1)
        sim.run(9000)
        self.assertEqual(len(arrivals), 8)
        self.assertAlmostEqual(max(arrivals), 8001.768)

    def test_source_crash_discards_unflushed_batch(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a"), quiet_node("b")],
                      {("a", "b"): Edge(1)}, batch_bytes=1000, batch_wait_us=10)
        arrivals = []
        net.send("a", "b", 20, lambda: arrivals.append(sim.now), reliable=False)
        sim.at(2, lambda: net.crash("a"))
        sim.at(3, lambda: net.recover("a"))
        sim.run(100)
        self.assertFalse(arrivals)
        self.assertEqual(sim.count["discarded_stale_batch_messages"], 1)
        self.assertEqual(sum(net.pending_bytes.values()), 0)

    def test_lost_ack_retries_without_repeating_same_incarnation_callback(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a"), quiet_node("b")],
                      {("a", "b"): Edge(1), ("b", "a"): Edge(1, loss=1)},
                      retry_us=10, max_retries=2)
        arrivals = []
        transfer = net.send("a", "b", 20, lambda: arrivals.append(sim.now))
        sim.run(100)
        self.assertEqual(len(arrivals), 1)
        self.assertEqual(transfer.attempts, 3)
        self.assertFalse(transfer.acked)
        self.assertEqual(sim.count["duplicate_messages"], 2)
        self.assertEqual(sim.count["retry_exhausted"], 1)

    def test_departed_packet_survives_source_crash_in_fabric(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a"), quiet_node("b")],
                      {("a", "b"): Edge(10, pool="slow")},
                      pools={"slow": {"bytes_us": 1, "queue_bytes": 10000}})
        arrivals = []
        net.send("a", "b", 200, lambda: arrivals.append(sim.now), reliable=False)
        sim.at(1, lambda: net.crash("a"))
        sim.run(400)
        self.assertEqual(len(arrivals), 1)
        self.assertAlmostEqual(arrivals[0], .296 + 10 + 296)

    def test_source_crash_during_transmit_does_not_deliver_full_packet(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a", rate=1), quiet_node("b")],
                      {("a", "b"): Edge(10)})
        arrivals = []
        net.send("a", "b", 200, lambda: arrivals.append(sim.now), reliable=False)
        sim.at(20, lambda: net.crash("a"))
        sim.run(400)
        self.assertFalse(arrivals)
        self.assertAlmostEqual(sim.count["wire_bytes"], 20)

    def test_fabric_overflow_does_not_erase_sent_bytes(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a"), quiet_node("b")],
                      {("a", "b"): Edge(1, pool="tiny", price_per_gb=1)},
                      pools={"tiny": {"bytes_us": 1000, "queue_bytes": 100}})
        arrivals = []
        net.send("a", "b", 200, lambda: arrivals.append(sim.now), reliable=False)
        sim.run(100)
        self.assertFalse(arrivals)
        self.assertEqual(sim.count["overflow:fabric:tiny"], 1)
        self.assertAlmostEqual(sim.count["wire_bytes"], 296)
        self.assertAlmostEqual(sim.count["price"], 296 / 1e9)

    def test_storage_completion_slots_do_not_multiply_byte_bandwidth(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a", persist_us=10, persist_slots=8,
                                      persist_bytes_us=1000)], {})
        completions = []
        for _ in range(8):
            net.persist("a", 1000000, lambda: completions.append(sim.now))
        sim.run(8100)
        self.assertEqual(completions, [1010 + 1000 * i for i in range(8)])
        self.assertAlmostEqual(net.disk_bytes["a"].busy_us, 8000)

    def test_storage_completion_latency_has_bounded_parallelism(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a", persist_us=100, persist_slots=2,
                                      persist_bytes_us=1000)], {})
        completions = []
        for _ in range(4):
            net.persist("a", 1, lambda: completions.append(sim.now))
        sim.run(210)
        self.assertEqual(len(completions), 4)
        self.assertAlmostEqual(completions[0], 100.001)
        self.assertAlmostEqual(completions[-1], 200.002)

    def test_large_batch_can_reassemble_longer_than_retry_interval(self):
        sim = Sim()
        net = Network(sim, [quiet_node("a", rate=10), quiet_node("b", rate=10)],
                      {("a", "b"): Edge(1)}, retry_us=10)
        arrivals = []
        net.send("a", "b", 10000, lambda: arrivals.append(sim.now), reliable=False)
        sim.run(1300)
        self.assertEqual(len(arrivals), 1)
        self.assertEqual(net.reassembly_bytes["b"], 0)

    def test_missing_fragment_reassembly_remains_finite(self):
        sim = Sim()
        edge = Edge(1)
        net = Network(sim, [quiet_node("a", rate=10), quiet_node("b", rate=10)],
                      {("a", "b"): edge}, retry_us=10)
        arrivals = []
        net.send("a", "b", 10000, lambda: arrivals.append(sim.now), reliable=False)
        sim.at(150, lambda: setattr(edge, "blocked", True))
        sim.run(1300)
        self.assertFalse(arrivals)
        self.assertEqual(net.reassembly_bytes["b"], 0)
        self.assertEqual(sim.count["reassembly_expired"], 1)

    def test_report_charges_elapsed_not_future_active_service(self):
        sim = Sim()
        resource = Resource(sim, "test")
        resource.submit(100, 1, lambda: None)
        sim.run(20)
        report = resource_report(sim)["test"]
        self.assertEqual(report["service_us"], 20)
        self.assertEqual(report["scheduled_service_us"], 100)


if __name__ == "__main__":
    unittest.main()
