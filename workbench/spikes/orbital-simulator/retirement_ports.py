"""Probe-only physical ports missing from the learning spike.

Listing returns at most ``limit`` keys, not their record bodies. Retirement is
an atomic durable device operation. Both consume the existing disk queue,
bandwidth, completion latency and buffer leases. No application root policy
lives here. Listing is not a consistent snapshot under concurrent mutation;
the accompanying fixture deliberately serializes its collector and writer.
"""
from kernel import clone
from sim import Context, World, encoded_size


class RetirementContext(Context):
    def __init__(self, world, actor):
        super().__init__(world, actor)
        self._retirement_world = world

    def enumerate(self, prefix, after, limit, then):
        return self._retirement_world.directory(self.actor, prefix, after, limit, then)

    def retire(self, key, then):
        return self._retirement_world.retire(self.actor, key, then)


class RetirementWorld(World):
    def _handler(self, message):
        # The spike hardcodes Context in World._handler. A port factory would
        # avoid copying this dispatch shell in the maintained simulator.
        actor = message["actor"]
        if self._valid(actor, message["incarnation"]):
            process = self.actors[actor]
            cause = self.kernel.cause
            process.last_step = self._record("actor_step", actor=actor,
                incarnation=process.incarnation, input_kind=message["kind"],
                parents=[process.last_step])
            self.kernel.cause = process.last_step
            process.actor.on(RetirementContext(self, actor), message["kind"],
                             clone(message["data"]))
            self.kernel.cause = cause
            process.busy = False
        else:
            self._record("stale_completion", actor=actor, incarnation=message["incarnation"])
        for lease in message.get("held", []):
            self._unpin(lease)
        self._pump_actor(actor)

    def directory(self, actor, prefix, after, limit, then):
        if not 1 <= limit <= 16:
            raise ValueError("probe directory page bound must be in [1, 16]")
        storage = self.hosts[self.actors[actor].host].storage
        candidates = sorted(key for owner, key in storage
                            if owner == actor and key.startswith(prefix) and key > after)
        keys = candidates[:limit]
        value = dict(keys=keys, next=keys[-1] if len(candidates) > limit else None)
        return self._physical(actor, "enumerate", prefix, value, encoded_size(value), then)

    def retire(self, actor, key, then):
        # No application tombstone retained: physical durable deletion is the
        # capability being probed, not a proposed Orbital record format.
        return self._physical(actor, "retire", key, None, 32, then)

    def _physical(self, actor, mode, key, value, size, then):
        process = self.actors[actor]
        machine = self.hosts[process.host]
        if machine.disk_active + len(machine.disk_queue) >= machine.config.queue_limit:
            self._record("disk_refused", actor=actor, reason="queue")
            return False
        lease = self._reserve(process.host, actor, process.incarnation, size,
                              "disk", owner=False)
        if lease is None:
            return False
        self.disk_serial += 1
        request = dict(actor=actor, incarnation=process.incarnation, host=process.host,
            generation=machine.generation, mode=mode, key=key, value=clone(value),
            size=size, then=then, data={}, held=[lease], request=self.disk_serial)
        request["_origin"] = self._record("disk_submit", actor=actor, mode=mode,
                                           key=key, size=size, parents=[])
        machine.disk_requests[self.disk_serial] = request
        machine.disk_queue.append(request)
        self._pump_disk(process.host)
        return True

    def _disk_finish(self, request):
        if request["mode"] != "retire":
            return super()._disk_finish(request)
        machine = self.hosts[request["host"]]
        if machine.disk_requests.pop(request["request"], None) is None:
            self._record("retired_disk_completion", request=request["request"])
            return
        machine.disk_active -= 1
        ok = machine.up and machine.generation == request["generation"]
        if ok:
            prior = machine.storage.pop((request["actor"], request["key"]), (None, 0))
            machine.storage_used -= prior[1]
            self._record("durable_retire", actor=request["actor"], host=request["host"],
                         key=request["key"], released=prior[1])
        self._deliver(dict(actor=request["actor"], incarnation=request["incarnation"],
            kind=request["then"], data=dict(ok=ok, key=request["key"]), held=request["held"]))
        self._pump_disk(request["host"])
