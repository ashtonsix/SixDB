"""Black-box checks of the simulator boundary, not an Orbital protocol proof.

Run directly with Python's unittest runner. Costs are deliberately tiny authored
nanosecond values, chosen to expose causality and conservation rather than model
hardware. Actors use only Context; observer assertions may inspect World.
"""

import unittest

from sim import Actor, Host, Link, World


def marks(world, label=None):
    return [
        row for row in world.trace
        if row["kind"] == "test_mark"
        and (label is None or row.get("label") == label)
    ]


def mark(ctx, label, **fields):
    ctx.note("test_mark", label=label, who=ctx.actor,
             generation=ctx.incarnation, observed_time=ctx.now, **fields)


def host(name, **overrides):
    options = dict(handler_ns=0, disk_latency=100,
                   disk_bytes_per_ns=1, nic_bytes_per_ns=1)
    options.update(overrides)
    return Host(name, **options)


class Recorder(Actor):
    def on(self, ctx, kind, data):
        if kind == "boot":
            mark(ctx, "boot")
        else:
            mark(ctx, kind, payload=data)


class LocalPolicy(Actor):
    """Its observations cannot distinguish hidden remote failures."""

    def on(self, ctx, kind, data):
        if kind == "boot":
            ctx.timer(10, "attempt")
        elif kind == "attempt":
            accepted = ctx.send("peer", "ping", {"value": 7}, size=8)
            mark(ctx, "attempt", accepted=accepted)
            ctx.timer(10, "local_timeout")
        elif kind == "local_timeout":
            mark(ctx, "local_timeout")


class Sender(Actor):
    def on(self, ctx, kind, data):
        if kind == "send":
            payload = {"nested": {"value": 7}}
            accepted = ctx.send(data.get("target", "receiver"), "packet",
                                payload, size=data.get("size", 10))
            payload["nested"]["value"] = 99
            mark(ctx, "sent", accepted=accepted)


class ResourceActor(Actor):
    def on(self, ctx, kind, data):
        if kind == "boot":
            mark(ctx, "boot")
        elif kind == "reserve":
            lease = ctx.reserve(data["size"], "probe")
            mark(ctx, "reserve", accepted=lease is not None)
            if lease is not None:
                ctx.release(lease)
        elif kind == "retain":
            lease = ctx.reserve(data["size"], "retained-compute")
            accepted = lease is not None and ctx.compute(
                data["duration"], "retired", leases=(lease,))
            mark(ctx, "retained", accepted=accepted)
            if lease is not None:
                ctx.release(lease)
            ctx.timer(50, "old_timer")
        elif kind == "jobs":
            for i in range(data["count"]):
                accepted = ctx.compute(data["duration"], "job_done", {"job": i})
                mark(ctx, "job_submitted", accepted=accepted, job=i)
        elif kind == "memory":
            first = ctx.reserve(8, "first")
            second = ctx.reserve(4, "over-budget")
            mark(ctx, "memory_admission", first=first is not None,
                 second=second is not None)
            if first is not None:
                ctx.release(first)
            third = ctx.reserve(4, "after-release")
            mark(ctx, "memory_after_release", accepted=third is not None)
            if second is not None:
                ctx.release(second)
            if third is not None:
                ctx.release(third)
        elif kind == "denied_lease":
            first = ctx.compute(100, "job_done", {"job": 0})
            lease = ctx.reserve(10, "rejected-job")
            second = lease is not None and ctx.compute(
                100, "job_done", {"job": 1}, leases=(lease,))
            if lease is not None:
                ctx.release(lease)
            mark(ctx, "denied_lease", first=first, second=second)
        elif kind in {"job_done", "retired", "old_timer", "tick"}:
            mark(ctx, kind, **data)


class StorageActor(Actor):
    def on(self, ctx, kind, data):
        if kind == "boot":
            ctx.load("saved", "loaded", {"reason": "boot"})
        elif kind == "save":
            value = data.get("value", {"n": 7})
            accepted = ctx.persist(data.get("key", "saved"), value,
                                   "saved", size=data.get("size", 10))
            mark(ctx, "write_submitted", accepted=accepted)
            if isinstance(value, dict):
                value["n"] = 99
        elif kind == "inspect":
            ctx.load("saved", "loaded", {"reason": "inspect"})
        elif kind == "fill":
            a = ctx.persist("a", "a", "saved", size=6)
            b = ctx.persist("b", "b", "saved", size=6)
            mark(ctx, "durable_admission", first=a, second=b)
        elif kind in {"saved", "loaded"}:
            mark(ctx, kind, **data)


class MixedUsers(Actor):
    def on(self, ctx, kind, data):
        if kind == "start":
            lease = ctx.reserve(10, "compute-input")
            compute = lease is not None and ctx.compute(
                1000, "finished", leases=(lease,))
            if lease is not None:
                ctx.release(lease)
            disk = ctx.persist("saved", {"n": 7}, "finished", size=10)
            send = ctx.send("peer", "packet", {"n": 7}, size=10)
            mark(ctx, "mixed_started", compute=compute, disk=disk, send=send)
        elif kind == "finished":
            mark(ctx, "unexpected_old_completion")


class DeliveryOrigin(Actor):
    def __init__(self, unsafe=False):
        self.unsafe = unsafe

    def on(self, ctx, kind, data):
        if kind == "start":
            ctx.wait("delivery", "awaiting application acknowledgement")
            accepted = ctx.send("sink", "packet", {"op": "delivery"}, size=10)
            if accepted and self.unsafe:
                # Deliberately wrong policy for the independent negative control.
                mark(ctx, "complete", op="delivery")
                ctx.clear_wait("delivery")
        elif kind == "ack" and not self.unsafe:
            mark(ctx, "complete", op=data["op"])
            ctx.clear_wait(data["op"])


class DeliverySink(Actor):
    def on(self, ctx, kind, data):
        if kind == "packet":
            mark(ctx, "receipt", op=data["op"])
            ctx.send("origin", "ack", data, size=10)


class CausalSender(Actor):
    def on(self, ctx, kind, data):
        if kind == "start":
            # The origin intentionally does not annotate the operation ID in
            # telemetry. The causal edge must come through the actual message.
            ctx.send("sink", "packet", {"op": "queued-delivery"}, size=10)


class CausalSink(Actor):
    def on(self, ctx, kind, data):
        if kind == "packet":
            mark(ctx, "receipt", op=data["op"])


class WaitReporter(Actor):
    def on(self, ctx, kind, data):
        if kind == "wait":
            ctx.wait("obligation", "waiting for source")
        elif kind == "finish":
            ctx.clear_wait("obligation")
            mark(ctx, "complete", op="obligation")


def assert_completed_deliveries_have_receipts(world):
    """Independent observation check; does not trust the sender's done flag."""
    receipts = marks(world, "receipt")
    for complete in marks(world, "complete"):
        if not any(r["op"] == complete["op"] and r["id"] < complete["id"]
                   for r in receipts):
            raise AssertionError(f"Completion {complete['id']} has no preceding receipt")


class SimulatorInvariants(unittest.TestCase):
    def world(self, **options):
        return World(seed=37, **options)

    def test_hidden_remote_failure_cannot_change_local_acceptance(self):
        def run(remote_dead):
            w = self.world()
            w.add_host(host("a"))
            w.add_host(host("b"))
            w.add_link(Link("a", "b", latency=10_000))
            w.add_actor("policy", "a", LocalPolicy)
            w.add_actor("peer", "b", Recorder)
            if remote_dead:
                w.crash("peer")
            w.run(until=100)
            return [(r["label"], r["observed_time"], r.get("accepted"))
                    for r in marks(w) if r["who"] == "policy"]

        healthy = run(False)
        self.assertEqual(healthy, run(True))
        self.assertEqual(healthy, [("attempt", 10, True),
                                   ("local_timeout", 20, None)])

    def test_departed_packet_survives_sender_process_or_machine_loss(self):
        for failure in ("crash", "power_loss", "destroy"):
            with self.subTest(failure=failure):
                w = self.world()
                w.add_host(host("a"))
                w.add_host(host("b"))
                w.add_link(Link("a", "b", latency=10_000, bandwidth=1))
                w.add_actor("sender", "a", Sender)
                w.add_actor("receiver", "b", Recorder)
                w.inject("sender", "send", {}, at=0)
                w.run(until=1_000)  # Bytes have left; propagation remains pending.
                self.assertEqual(len(marks(w, "packet")), 0)
                self.assertTrue(marks(w, "sent")[0]["accepted"])
                getattr(w, failure)("sender" if failure == "crash" else "a")
                w.run(until=20_000)
                packets = marks(w, "packet")
                self.assertEqual(len(packets), 1)
                self.assertEqual(packets[0]["payload"], {"nested": {"value": 7}})

    def test_queued_actor_message_keeps_its_sender_in_causal_slice(self):
        w = self.world()
        w.add_host(host("a"))
        w.add_host(host("b", handler_ns=100))
        w.add_link(Link("a", "b", latency=0, bandwidth=1))
        w.add_actor("origin", "a", CausalSender)
        w.add_actor("sink", "b", CausalSink)
        w.inject("origin", "start", {}, at=0)
        w.run(until=1_000)
        self.assertEqual(len(marks(w, "receipt")), 1)
        history = w.explain("queued-delivery")["causal_history"]
        self.assertTrue(any(row["kind"] == "actor_step"
                            and row.get("actor") == "origin"
                            and row.get("input_kind") == "start" for row in history),
                        "Inbox waiting must not erase the incoming message's causal origin")

    def test_negative_control_rejects_enqueue_as_remote_completion(self):
        def scenario(*, loss, unsafe):
            w = self.world()
            w.add_host(host("a"))
            w.add_host(host("b"))
            w.add_link(Link("a", "b", latency=10, loss=loss))
            w.add_link(Link("b", "a", latency=10))
            w.add_actor("origin", "a", lambda: DeliveryOrigin(unsafe=unsafe))
            w.add_actor("sink", "b", DeliverySink)
            w.inject("origin", "start", {}, at=0)
            w.run(until=1_000)
            return w

        healthy = scenario(loss=0, unsafe=False)
        self.assertEqual(len(marks(healthy, "complete")), 1)
        assert_completed_deliveries_have_receipts(healthy)
        pending = scenario(loss=1, unsafe=False)
        self.assertEqual(marks(pending, "complete"), [])
        self.assertEqual(len(pending.explain("delivery")["waits"]), 1)
        assert_completed_deliveries_have_receipts(pending)
        broken = scenario(loss=1, unsafe=True)
        evidence = list(broken.trace)
        with self.assertRaisesRegex(AssertionError, "no preceding receipt"):
            assert_completed_deliveries_have_receipts(broken)
        self.assertEqual(broken.trace, evidence)  # Failed checking preserves evidence.

    def test_stale_callbacks_do_not_reach_restarted_actor(self):
        w = self.world()
        w.add_host(host("h", memory_bytes=10, workers=1))
        w.add_actor("worker", "h", ResourceActor)
        w.add_actor("observer", "h", ResourceActor)
        w.inject("worker", "retain", {"size": 10, "duration": 100}, at=0)
        w.run(until=20)
        self.assertTrue(marks(w, "retained")[0]["accepted"])
        w.crash("worker")
        w.restart("worker")
        w.inject("observer", "reserve", {"size": 10}, at=25)
        w.run(until=60)
        self.assertFalse(marks(w, "reserve")[0]["accepted"])
        self.assertEqual(marks(w, "old_timer"), [])
        w.inject("observer", "reserve", {"size": 10}, at=110)
        w.run(until=200)
        self.assertTrue(marks(w, "reserve")[1]["accepted"])
        self.assertEqual(marks(w, "retired"), [])
        boots = [r for r in marks(w, "boot") if r["who"] == "worker"]
        self.assertEqual(len(boots), 2)
        self.assertNotEqual(boots[0]["generation"], boots[1]["generation"])

    def test_memory_admission_is_finite_and_release_returns_capacity(self):
        w = self.world()
        w.add_host(host("h", memory_bytes=10))
        w.add_actor("worker", "h", ResourceActor)
        w.inject("worker", "memory", {}, at=0)
        w.run(until=100)
        admission = marks(w, "memory_admission")[0]
        self.assertTrue(admission["first"])
        self.assertFalse(admission["second"])
        self.assertTrue(marks(w, "memory_after_release")[0]["accepted"])

    def test_colocated_actors_share_worker_capacity(self):
        w = self.world()
        w.add_host(host("h", workers=1))
        for name in ("a", "b"):
            w.add_actor(name, "h", ResourceActor)
            w.inject(name, "jobs", {"count": 1, "duration": 100}, at=0)
        w.run(until=500)
        self.assertTrue(all(r["accepted"] for r in marks(w, "job_submitted")))
        times = sorted(r["observed_time"] for r in marks(w, "job_done"))
        self.assertEqual(len(times), 2)
        self.assertGreaterEqual(times[0], 100)
        self.assertGreaterEqual(times[1] - times[0], 100)

    def test_resource_report_includes_work_incurred_before_cutoff(self):
        w = self.world()
        w.add_host(host("h", workers=1))
        w.add_actor("worker", "h", ResourceActor)
        w.inject("worker", "jobs", {"count": 1, "duration": 100}, at=0)
        w.run(until=50)
        usage = w.report()["resources"]["h/workers"]
        self.assertEqual(usage["completed"], 0)
        self.assertEqual(usage["active"], 1)
        self.assertEqual(usage["busy_ns"], 50)
        w.run(until=100)
        self.assertEqual(w.report()["resources"]["h/workers"]["busy_ns"], 100)

    def test_pause_drains_active_work_and_blocks_colocated_new_work(self):
        w = self.world()
        w.add_host(host("h", workers=1, memory_bytes=20))
        w.add_host(host("other", workers=1))
        for actor, machine in (("a", "h"), ("b", "h"),
                               ("c", "h"), ("independent", "other")):
            w.add_actor(actor, machine, ResourceActor)
        for actor in ("a", "b"):
            w.inject(actor, "retain", {"size": 10, "duration": 100}, at=0)
        w.run(until=50)
        w.pause("h", "workers", True)
        w.run(until=150)
        completed = marks(w, "retired")
        self.assertEqual([(r["who"], r["observed_time"]) for r in completed],
                         [("a", 100)])
        report = w.report()
        self.assertEqual(report["hosts"]["h"]["memory_used"], 10)
        self.assertEqual(report["resources"]["h/workers"]["active"], 0)
        self.assertEqual(report["resources"]["h/workers"]["queued"], 1)
        self.assertEqual(report["resources"]["h/workers"]["busy_ns"], 100)
        for actor in ("c", "independent"):
            w.inject(actor, "jobs", {"count": 1, "duration": 20}, at=151)
        w.run(until=190)
        self.assertEqual([(r["who"], r["observed_time"])
                          for r in marks(w, "job_done")], [("independent", 171)])
        self.assertEqual(w.report()["resources"]["h/workers"]["queued"], 2)
        w.pause("h", "workers", False)
        w.run(until=400)
        self.assertEqual([(r["who"], r["observed_time"])
                          for r in marks(w, "retired")], [("a", 100), ("b", 290)])
        local = [r for r in marks(w, "job_done") if r["who"] == "c"]
        self.assertEqual([r["observed_time"] for r in local], [310])
        report = w.report()
        self.assertEqual(report["hosts"]["h"]["memory_used"], 0)
        self.assertEqual(report["resources"]["h/workers"]["busy_ns"], 220)
        self.assertEqual(report["resources"]["h/workers"]["completed"], 3)

    def test_slowdown_uses_factor_at_start_and_preserves_active_cost(self):
        w = self.world()
        w.add_host(host("h", workers=1))
        w.add_actor("worker", "h", ResourceActor)
        w.add_actor("later", "h", ResourceActor)
        w.inject("worker", "jobs", {"count": 2, "duration": 100}, at=0)
        w.run(until=50)
        w.slowdown("h", "workers", 4)
        w.run(until=150)
        self.assertEqual([r["observed_time"] for r in marks(w, "job_done")], [100])
        w.slowdown("h", "workers", 1)
        w.inject("later", "jobs", {"count": 1, "duration": 10}, at=151)
        w.run(until=600)
        self.assertEqual([(r["who"], r["observed_time"])
                          for r in marks(w, "job_done")],
                         [("worker", 100), ("worker", 500), ("later", 510)])
        self.assertEqual(w.report()["resources"]["h/workers"]["busy_ns"], 510)

    def test_crash_retires_wait_report_without_completing_obligation(self):
        w = self.world()
        w.add_host(host("h"))
        w.add_actor("worker", "h", WaitReporter)
        w.inject("worker", "wait", {}, at=0)
        w.run(until=10)
        old = w.report()["waits"][0]
        self.assertTrue(old["reporting_incarnation_alive"])
        w.crash("worker")
        report = w.report()
        self.assertEqual(report["waits"], [])
        self.assertEqual(len(report["retired_waits"]), 1)
        self.assertEqual(report["retired_waits"][0]["incarnation"], old["incarnation"])
        self.assertEqual(report["counts"].get("wait_cleared", 0), 0)
        self.assertEqual(marks(w, "complete"), [])
        w.restart("worker")
        w.inject("worker", "wait", {}, at=11)
        w.run(until=20)
        current = w.report()["waits"][0]
        self.assertNotEqual(current["incarnation"], old["incarnation"])
        self.assertTrue(current["reporting_incarnation_alive"])
        self.assertEqual(w.explain("obligation")["waits"], [current])
        w.inject("worker", "finish", {}, at=21)
        w.run(until=30)
        report = w.report()
        self.assertEqual(report["waits"], [])
        self.assertEqual(len(report["retired_waits"]), 1)
        self.assertEqual(len(marks(w, "complete")), 1)
        self.assertEqual(marks(w, "complete")[0]["generation"], current["incarnation"])

    def test_handler_execution_consumes_control_capacity(self):
        w = self.world()
        w.add_host(host("h", control=1, handler_ns=10))
        for name in ("a", "b", "c"):
            w.add_actor(name, "h", ResourceActor)
            w.inject(name, "tick", {}, at=0)
        w.run(until=500)
        times = sorted(r["observed_time"] for r in marks(w, "tick"))
        self.assertEqual(len(times), 3)
        self.assertTrue(all(b - a >= 10 for a, b in zip(times, times[1:])))

    def test_one_actor_does_not_gain_parallel_handlers_from_control_pool(self):
        w = self.world()
        w.add_host(host("h", control=4, handler_ns=10))
        w.add_actor("worker", "h", ResourceActor)
        for _ in range(3):
            w.inject("worker", "tick", {}, at=0)
        w.run(until=500)
        times = sorted(r["observed_time"] for r in marks(w, "tick"))
        self.assertEqual(len(times), 3)
        self.assertTrue(all(b - a >= 10 for a, b in zip(times, times[1:])))

    def test_finite_worker_queue_refuses_without_losing_accepted_work(self):
        w = self.world()
        w.add_host(host("h", workers=1, queue_limit=1))
        w.add_actor("worker", "h", ResourceActor)
        w.run(until=1)
        w.inject("worker", "jobs", {"count": 4, "duration": 100}, at=2)
        w.run(until=1_000)
        accepted = [r["job"] for r in marks(w, "job_submitted") if r["accepted"]]
        self.assertGreaterEqual(len(accepted), 1)
        self.assertLessEqual(len(accepted), 2)  # One active and at most one queued.
        self.assertCountEqual(accepted, [r["job"] for r in marks(w, "job_done")])

    def test_rejected_compute_releases_its_temporary_lease_reference(self):
        w = self.world()
        w.add_host(host("h", workers=1, queue_limit=1, memory_bytes=10))
        w.add_actor("worker", "h", ResourceActor)
        w.run(until=1)
        w.inject("worker", "denied_lease", {}, at=2)
        w.run(until=3)
        admission = marks(w, "denied_lease")[0]
        self.assertTrue(admission["first"])
        self.assertFalse(admission["second"])
        self.assertEqual(w.report()["hosts"]["h"]["memory_used"], 0)
        w.run(until=200)
        self.assertEqual([r["job"] for r in marks(w, "job_done")], [0])

    def test_distinct_links_share_source_nic_capacity(self):
        w = self.world()
        for name in ("a", "b", "c"):
            w.add_host(host(name, nic_bytes_per_ns=1))
        for target in ("b", "c"):
            w.add_link(Link("a", target, latency=0, bandwidth=1000))
            w.add_actor(target, target, Recorder)
        w.add_actor("sender", "a", Sender)
        for target in ("b", "c"):
            w.inject("sender", "send", {"target": target, "size": 1000}, at=0)
        w.run(until=10_000)
        times = sorted(r["observed_time"] for r in marks(w, "packet"))
        self.assertEqual(len(times), 2)
        self.assertGreaterEqual(times[-1], 2000)
        self.assertGreaterEqual(times[1] - times[0], 1000)

    def test_distinct_sources_share_receiver_nic_capacity(self):
        w = self.world()
        w.add_host(host("r", nic_bytes_per_ns=1))
        w.add_actor("receiver", "r", Recorder)
        for source in ("a", "b"):
            w.add_host(host(source, nic_bytes_per_ns=1000))
            w.add_link(Link(source, "r", latency=0, bandwidth=1000))
            w.add_actor(source, source, Sender)
            w.inject(source, "send", {"size": 1000}, at=0)
        w.run(until=10_000)
        times = sorted(r["observed_time"] for r in marks(w, "packet"))
        self.assertEqual(len(times), 2)
        self.assertGreaterEqual(times[-1], 2000)
        self.assertGreaterEqual(times[1] - times[0], 1000)

    def storage_world(self, **options):
        w = self.world()
        w.add_host(host("h", **options))
        w.add_actor("store", "h", StorageActor)
        w.run(until=1_000)  # Initial boot read is not the write under test.
        return w

    def test_process_death_does_not_cancel_submitted_device_write(self):
        w = self.storage_world()
        w.inject("store", "save", {}, at=1_001)
        w.run(until=1_005)
        self.assertTrue(marks(w, "write_submitted")[-1]["accepted"])
        w.crash("store")
        w.run(until=2_000)
        self.assertEqual(marks(w, "saved"), [])
        w.restart("store")
        w.run(until=3_000)
        self.assertEqual(marks(w, "loaded")[-1]["value"], {"n": 7})

    def test_power_loss_cancels_uncompleted_device_write(self):
        w = self.storage_world()
        w.inject("store", "save", {}, at=1_001)
        w.run(until=1_005)
        w.power_loss("h")
        w.run(until=2_000)
        w.power_on("h")
        w.run(until=3_000)
        self.assertIsNone(marks(w, "loaded")[-1]["value"])
        self.assertEqual(marks(w, "saved"), [])

    def test_old_device_completion_cannot_overwrite_post_power_cycle_write(self):
        w = self.storage_world()
        w.inject("store", "save", {"value": {"n": 1}, "size": 1000}, at=1_001)
        w.run(until=1_005)
        w.power_loss("h")
        w.power_on("h")
        w.inject("store", "save", {"value": {"n": 2}}, at=1_006)
        w.run(until=2_000)
        self.assertEqual(len(marks(w, "saved")), 1,
                         "Cancelled old device work must not occupy post-reset service")
        self.assertTrue(marks(w, "saved")[0]["ok"])
        w.inject("store", "inspect", {}, at=2_001)
        w.run(until=3_000)
        self.assertEqual(marks(w, "loaded")[-1]["value"], {"n": 2})
        self.assertEqual(w.durable("h", "store"), {"saved": {"n": 2}})

    def test_power_loss_retires_work_in_device_latency_phase(self):
        w = self.storage_world()
        w.inject("store", "save", {}, at=1_001)
        w.run(until=1_015)  # Byte service finished; persistence latency did not.
        self.assertEqual(marks(w, "saved"), [])
        self.assertGreater(w.report()["hosts"]["h"]["memory_used"], 0)
        w.power_loss("h")
        status = w.report()["hosts"]["h"]
        self.assertEqual(status["memory_used"], 0)
        self.assertEqual(status["durable_reserved"], 0)
        self.assertEqual(status["disk_pending"], 0)
        w.power_on("h")
        w.run(until=3_000)
        self.assertIsNone(marks(w, "loaded")[-1]["value"])
        self.assertEqual(w.report()["hosts"]["h"]["memory_used"], 0)

    def test_power_loss_retires_mixed_users_once_and_preserves_spent_work(self):
        w = self.world()
        w.add_host(host("h", memory_bytes=30, workers=1,
                        nic_bytes_per_ns=.01, disk_bytes_per_ns=.01))
        w.add_host(host("remote"))
        w.add_link(Link("h", "remote", latency=1000))
        w.add_actor("worker", "h", MixedUsers)
        w.add_actor("peer", "remote", Recorder)
        w.inject("worker", "start", {}, at=0)
        w.run(until=50)
        started = marks(w, "mixed_started")[0]
        self.assertTrue(all(started[k] for k in ("compute", "disk", "send")))
        self.assertEqual(w.report()["hosts"]["h"]["memory_used"], 30)
        w.power_loss("h")
        report = w.report()
        self.assertEqual(report["hosts"]["h"]["memory_used"], 0)
        self.assertEqual(report["hosts"]["h"]["durable_reserved"], 0)
        for resource in ("h/workers", "h/tx", "h/disk_bytes"):
            self.assertEqual(report["resources"][resource]["busy_ns"], 50)
            self.assertEqual(report["resources"][resource]["active"], 0)
        w.power_on("h")
        w.run(until=5_000)
        self.assertEqual(marks(w, "unexpected_old_completion"), [])
        self.assertEqual(marks(w, "packet"), [])  # Send had not departed.
        self.assertEqual(w.report()["hosts"]["h"]["memory_used"], 0)

    def test_completed_write_survives_power_loss_but_not_destruction(self):
        w = self.storage_world()
        w.inject("store", "save", {}, at=1_001)
        w.run(until=2_000)
        self.assertTrue(marks(w, "saved")[-1]["ok"])
        w.power_loss("h")
        w.power_on("h")
        w.run(until=3_000)
        self.assertEqual(marks(w, "loaded")[-1]["value"], {"n": 7})
        w.destroy("h")
        self.assertEqual(w.durable("h", "store"), {})
        w.power_on("h")
        w.run(until=4_000)
        self.assertIsNone(marks(w, "loaded")[-1]["value"])

    def test_pending_writes_reserve_durable_capacity(self):
        w = self.storage_world(durable_bytes=10)
        w.inject("store", "fill", {}, at=1_001)
        w.run(until=2_000)
        admission = marks(w, "durable_admission")[0]
        self.assertTrue(admission["first"])
        self.assertFalse(admission["second"])
        self.assertEqual(w.durable("h", "store"), {"a": "a"})

    def test_io_slots_do_not_multiply_device_byte_bandwidth(self):
        w = self.storage_world(io_slots=8, disk_bytes_per_ns=1)
        for key in ("a", "b"):
            w.inject("store", "save", {"key": key, "size": 1000}, at=1_001)
        w.run(until=10_000)
        completions = marks(w, "saved")
        self.assertEqual(len(completions), 2)
        self.assertGreaterEqual(max(r["observed_time"] for r in completions), 3_001)

    def schedule_world(self, *, replay=None, extra=False):
        options = {"ordering": "shuffle"}
        if replay is not None:
            options["replay"] = replay
        w = self.world(**options)
        w.add_host(host("h", control=1))
        for name in ("a", "b", "c"):
            w.add_actor(name, "h", Recorder)
            w.inject(name, "scheduled", {"name": name}, at=100)
        if extra:
            w.inject("a", "unexpected", {}, at=100)
        return w

    def test_schedule_decisions_replay_exactly(self):
        first = self.schedule_world()
        first.run(until=1_000)
        self.assertTrue(first.decisions, "The fixture must exercise a scheduling choice")
        replay = self.schedule_world(replay=first.decisions)
        replay.run(until=1_000)
        self.assertEqual(marks(first), marks(replay))
        self.assertEqual(first.decisions, replay.decisions)

    def test_schedule_replay_rejects_changed_enabled_events(self):
        first = self.schedule_world()
        first.run(until=1_000)
        with self.assertRaises(ValueError):
            changed = self.schedule_world(replay=first.decisions, extra=True)
            changed.run(until=1_000)


if __name__ == "__main__":
    unittest.main()
