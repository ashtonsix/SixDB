"""Bounded reconstruction/reclamation fixture, not an Orbital GC protocol.

One serialized durable owner, two explicit replay roots, immutable records,
atomic record writes/deletion, and a fixed 4-KiB actor working envelope. Bytes
cross the JSON spike ports as hex; this tests actual reconstruction, not native
mapping, pointer aliasing, page faults, decoder execution or COW.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path

from retirement_ports import RetirementWorld
from scenario import Fault, Input, Scenario
from sim import Actor, Host


BASE = bytes(range(256)) * 4
MASKS = (17, 33, 65, 129)
UNTIL = 8_000_000


def descriptor(version):
    return dict(version=version, base="data/base/0", codec="data/codec/xor-v1",
                operations=[f"data/op/{i}" for i in range(1, version + 1)])


def references(value):
    return {value["base"], value["codec"], *value["operations"]}


def initial_records():
    return [("data/base/0", dict(version=0, bytes=BASE.hex())),
        ("data/codec/xor-v1", dict(opcode="xor_each_byte_v1")),
        ("data/codec/raw-v1", dict(opcode="raw_bytes_v1")),
        *[(f"data/op/{i}", dict(version=i, mask=mask))
          for i, mask in enumerate(MASKS, 1)],
        ("data/garbage", dict(bytes=(b"unreachable" * 32).hex())),
        ("head", descriptor(4)), ("roots/reader", descriptor(2)),
        ("roots/replay", descriptor(4))]


@dataclass(frozen=True)
class Case:
    root: str = "reader"
    fault: str = "none"  # none, checkpoint-reset
    negative: str = "none"  # ignore-root, premature-checkpoint
    listing: str = "paged"  # paged, whole-prefix


class MissingRecord(Exception):
    pass


class Owner(Actor):
    def __init__(self, case):
        self.case = case
        self.flow = None
        self.envelope = None

    def on(self, ctx, kind, data):
        if kind == "boot":
            self.envelope = ctx.reserve(4096, "bounded-owner-workspace")
            if self.envelope is None:
                raise RuntimeError("fixture owner workspace unavailable")
            ctx.note("retirement_boot", recovered_values=0)
        elif kind == "io":
            if not data["ok"]:
                raise RuntimeError("fixture IO failed without device reset")
            self._advance(ctx, data)
        elif kind == "late-checkpoint":
            ctx.persist("data/checkpoint/4", data, "unused")
        elif kind == "unused":
            pass
        else:
            if self.flow is not None:
                raise RuntimeError("serialized fixture received overlapping application work")
            self.flow = getattr(self, kind.replace("-", "_"))(ctx, data)
            self._advance(ctx)

    def _advance(self, ctx, result=None):
        try:
            command = self.flow.send(result)
        except StopIteration:
            self.flow = None
            return
        except MissingRecord as error:
            self.flow = None
            ctx.note("reconstruction_failed", missing=str(error))
            return
        kind, *args = command
        if not getattr(ctx, kind)(*args, then="io"):
            # Retain the unfinished continuation and expose the resource wait;
            # this fixture has no actor capable of releasing its own workspace
            # while a whole-prefix scan would require more than the free pool.
            ctx.wait("retirement", "physical port refused", operation=kind)

    def initialize(self, ctx, data):
        # Initial data is authored external input, never constructor recovery.
        for key, value in data["records"]:
            yield ("persist", key, value)
        ctx.note("retirement_initialized")

    def load(self, key):
        result = yield ("load", key)
        if result["value"] is None:
            raise MissingRecord(key)
        return result["value"]

    def reconstruct(self, ctx, root):
        desc = yield from self.load(root)
        codec = yield from self.load(desc["codec"])
        base = yield from self.load(desc["base"])
        raw = bytes.fromhex(base["bytes"])
        version = base["version"]
        if codec["opcode"] not in ("xor_each_byte_v1", "raw_bytes_v1"):
            raise ValueError("unsupported fixture decoder")
        for key in desc["operations"]:
            if codec["opcode"] != "xor_each_byte_v1":
                raise ValueError("raw checkpoint cannot interpret operations")
            operation = yield from self.load(key)
            if operation["version"] != version + 1:
                raise ValueError("replay sequence hole")
            raw = bytes(byte ^ operation["mask"] for byte in raw)
            version = operation["version"]
        if version != desc["version"]:
            raise ValueError("incomplete reconstruction")
        ctx.note("bytes_reconstructed", root=root, version=version,
                 bytes=raw.hex(), dependencies=sorted(references(desc)))
        return raw

    def prepare(self, ctx, data):
        other = "reader" if self.case.root == "replay" else "replay"
        yield ("retire", f"roots/{other}")
        if self.case.negative == "premature-checkpoint":
            yield ("retire", f"roots/{self.case.root}")
        raw = yield from self.reconstruct(ctx, "head")
        checkpoint = dict(version=4, bytes=raw.hex())
        replacement = dict(version=4, base="data/checkpoint/4",
                           codec="data/codec/raw-v1", operations=[])
        if self.case.negative == "premature-checkpoint":
            # Deliberately wrong: publish the replacement before its bytes.
            yield ("persist", "head", replacement)
            ctx.timer(2_000_000, "late-checkpoint", checkpoint)
        else:
            yield ("persist", "data/checkpoint/4", checkpoint)
            yield ("persist", "head", replacement)
        yield from self.collect(ctx)
        ctx.note("retirement_collected", phase="before-reset")

    def page(self, ctx, prefix, after):
        if self.case.listing == "paged":
            result = yield ("enumerate", prefix, after, 2)
            page = result["records"]
            ctx.note("directory_page", prefix=prefix, count=len(page["keys"]))
            return page
        result = yield ("scan", prefix)
        return dict(keys=sorted(result["records"]), next=None)

    def collect(self, ctx):
        # Exactly two root types in this fixture. Reference metadata fits the
        # charged envelope; this is not unbounded production tracing GC.
        head = yield from self.load("head")
        keep = references(head)
        after = ""
        while True:
            page = yield from self.page(ctx, "roots/", after)
            for key in page["keys"]:
                value = yield from self.load(key)
                if self.case.negative != "ignore-root":
                    keep.update(references(value))
            if page["next"] is None:
                break
            after = page["next"]
        after = ""
        while True:
            page = yield from self.page(ctx, "data/", after)
            for key in page["keys"]:
                if key not in keep:
                    yield ("retire", key)
            if page["next"] is None:
                break
            after = page["next"]

    def delayed_read(self, ctx, data):
        root = "head" if self.case.negative == "premature-checkpoint" else f"roots/{self.case.root}"
        ctx.note("delayed_first_access", root=root)
        raw = yield from self.reconstruct(ctx, root)
        ctx.note("old_read_complete", root=root, bytes=raw.hex())
        yield ("retire", root)
        yield from self.collect(ctx)
        ctx.note("retirement_collected", phase="after-release")

    def verify_head(self, ctx, data):
        yield from self.reconstruct(ctx, "head")
        ctx.note("checkpoint_read_complete")


def build(case=Case(), seed=1, replay=None):
    if case.root not in ("reader", "replay") or case.fault not in ("none", "checkpoint-reset"):
        raise ValueError("unknown fixture case")
    if case.negative not in ("none", "ignore-root", "premature-checkpoint"):
        raise ValueError("unknown negative control")
    if case.listing not in ("paged", "whole-prefix"):
        raise ValueError("unknown directory port")
    world = RetirementWorld(seed=seed, ordering="shuffle", replay=replay)
    world.add_host(Host("storage", memory_bytes=8192, durable_bytes=16384))
    world.add_actor("owner", "storage", lambda: Owner(case))
    world.inject("owner", "initialize", dict(records=initial_records()), at=1000)
    scenario = Scenario(world)
    scenario.when("initialized", "retirement_initialized", lambda event: True,
                  [Input(0, "owner", "prepare", {})])
    if case.fault == "checkpoint-reset":
        scenario.when("checkpoint-durable-before-callback", "durable_write",
            lambda event: event["key"] == "data/checkpoint/4",
            [Fault(0, "power_loss", dict(host="storage")),
             Fault(10_000, "power_on", dict(host="storage")),
             Input(20_000, "owner", "prepare", {})])
    scenario.when("collected-before-first-access", "retirement_collected",
        lambda event: event["phase"] == "before-reset",
        [Fault(0, "power_loss", dict(host="storage")),
         Fault(10_000, "power_on", dict(host="storage")),
         Input(20_000, "owner", "delayed-read", {})])
    if case.negative == "none" and case.listing == "paged":
        scenario.when("old-dependencies-retired", "retirement_collected",
            lambda event: event["phase"] == "after-release",
            [Fault(0, "power_loss", dict(host="storage")),
             Fault(10_000, "power_on", dict(host="storage")),
             Input(20_000, "owner", "verify-head", {})])
    return world, scenario


def audit(world):
    """Independent prefix oracle: reconstruct durable inventory from receipts.

    Does not consult actor state, collector keep-sets or reconstruction helper.
    Fixture expected bytes are derived independently from authored XOR masks.
    """
    inventory, errors, resets = {}, [], 0
    roots_read, reconstructed = set(), {}
    combined = 0
    expected = {0: BASE.hex()}
    for version, mask in enumerate(MASKS, 1):
        combined ^= mask
        expected[version] = bytes(value ^ combined for value in BASE).hex()
    for event in world.trace:
        kind = event["kind"]
        if kind == "durable_write" and event["actor"] == "owner":
            key, value = event["key"], event["value"]
            if key == "head" and value["base"].startswith("data/checkpoint/"):
                prior = inventory.get(value["base"])
                if not prior or prior.get("bytes") != expected[value["version"]]:
                    errors.append("head published before matching checkpoint durable")
            inventory[key] = value
        elif kind == "durable_retire" and event["actor"] == "owner":
            key = event["key"]
            if key.startswith("data/"):
                for name, root in inventory.items():
                    if name == "head" or name.startswith("roots/"):
                        needed = [root["base"], root["codec"], *root["operations"]]
                        if key in needed:
                            errors.append(f"retired live dependency {key} of {name}")
            inventory.pop(key, None)
        elif kind == "power_loss" and event["host"] == "storage":
            resets += 1
        elif kind == "delayed_first_access":
            if not resets or event["root"] in roots_read:
                errors.append("old read was not first access after storage reset")
            roots_read.add(event["root"])
        elif kind == "bytes_reconstructed":
            if event["bytes"] != expected[event["version"]]:
                errors.append("reconstructed bytes differ from authored history")
            root = inventory.get(event["root"])
            if root is None or root["version"] != event["version"]:
                errors.append("reconstruction version differs from durable root")
            elif set(event["dependencies"]) != {root["base"], root["codec"], *root["operations"]}:
                errors.append("reconstruction dependencies differ from durable root")
            if any(key not in inventory for key in event["dependencies"]):
                errors.append("reconstruction used nondurable dependency")
            reconstructed[(event["root"], event["incarnation"])] = event["bytes"]
        elif kind == "old_read_complete":
            if reconstructed.get((event["root"], event["incarnation"])) != event["bytes"]:
                errors.append("old read completed without same-incarnation reconstruction")
        elif kind == "reconstruction_failed":
            errors.append("required reconstruction unavailable: " + event["missing"])
        elif kind == "directory_page" and event["count"] > 2:
            errors.append("directory page exceeded bound")
    return sorted(set(errors))


class Borrower(Actor):
    def __init__(self, omit_second_pin=False):
        self.omit_second_pin = omit_second_pin

    def on(self, ctx, kind, data):
        if kind == "start":
            payload = bytes.fromhex(data["bytes"])
            lease = ctx.reserve(len(payload), "reconstructed-view")
            if lease is None:
                raise RuntimeError("fixture frame allocation failed")
            for name, duration in (("A", 80_000), ("B", 160_000)):
                pinned = not (name == "B" and self.omit_second_pin)
                accepted = ctx.compute(duration, "complete",
                    dict(borrower=name, frame=lease, bytes=payload.hex()),
                    leases=(lease,) if pinned else ())
                if not accepted:
                    raise RuntimeError("fixture backend queue full")
                ctx.note("backend_borrow_submitted", borrower=name, frame=lease,
                         pinned=pinned, duration=duration, digest=hashlib.sha256(payload).hexdigest())
            ctx.release(lease)
            ctx.note("view_cancelled", frame=lease)
        elif kind == "probe":
            lease = ctx.reserve(4096, "whole-pool-reuse-probe")
            ctx.note("reuse_probe", phase=data["phase"], available=lease is not None)
            if lease is not None:
                ctx.release(lease)
        elif kind == "complete":
            ctx.note("backend_callback", borrower=data["borrower"])
        elif kind != "boot":
            raise ValueError(kind)


def borrow_case(omit_second_pin=False, seed=1, replay=None):
    world = RetirementWorld(seed=seed, ordering="shuffle", replay=replay)
    world.add_host(Host("backend", memory_bytes=4096, workers=2))
    world.add_actor("borrower", "backend", lambda: Borrower(omit_second_pin))
    # Real bytes are supplied; this separate ownership probe does not claim
    # that this fixture's input itself was recovered from storage.
    world.inject("borrower", "start", dict(bytes=BASE.hex()), at=1000)
    scenario = Scenario(world).when("cancelled", "view_cancelled", lambda event: True,
        [Fault(0, "crash", dict(actor="borrower")),
         Fault(1000, "restart", dict(actor="borrower")),
         Input(10_000, "borrower", "probe", dict(phase="both-running")),
         Input(100_000, "borrower", "probe", dict(phase="second-running")),
         Input(200_000, "borrower", "probe", dict(phase="both-retired"))])
    world.run(until=300_000)
    scenario.require_all()
    errors = []
    submitted = [event for event in world.trace if event["kind"] == "backend_borrow_submitted"]
    for event in submitted:
        if not event["pinned"]:
            errors.append("backend borrower lacks physical pin: " + event["borrower"])
    for event in world.trace:
        if event["kind"] == "reuse_probe":
            expected = event["phase"] == "both-retired"
            if event["available"] != expected:
                errors.append("frame reuse violates backend lifetime: " + event["phase"])
    return dict(omit_second_pin=omit_second_pin, seed=seed, errors=errors,
                probes=[{key: e[key] for key in ("phase", "available", "time")}
                        for e in world.trace if e["kind"] == "reuse_probe"],
                report=world.report()), world


def run_case(case=Case(), seed=1, replay=None):
    world, scenario = build(case, seed, replay)
    world.run(until=UNTIL)
    return dict(case=asdict(case), seed=seed, errors=audit(world),
        coverage=scenario.coverage(), report=world.report(),
        hosts={name: asdict(machine.config) for name, machine in world.hosts.items()},
        reads=[{key: event[key] for key in ("root", "version", "incarnation")}
               for event in world.trace if event["kind"] == "bytes_reconstructed"],
        reclaimed_bytes=sum(event["released"] for event in world.trace
                            if event["kind"] == "durable_retire"),
        inventory=sorted(key for actor, key in world.hosts["storage"].storage)), world


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    def source_hashes():
        return {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                for name in ("retirement_probe.py", "retirement_ports.py", "sim.py", "kernel.py", "scenario.py")}
    sources = source_hashes()
    cases = [Case(root=root, fault=fault) for root in ("reader", "replay")
             for fault in ("none", "checkpoint-reset")]
    cases += [Case(root=root, negative="ignore-root") for root in ("reader", "replay")]
    cases += [Case(negative="premature-checkpoint")]
    cases += [Case(listing="whole-prefix")]
    rows = [run_case(case, seed)[0] for case in cases for seed in (1, 7, 19)]
    borrowers = [borrow_case(negative, seed)[0] for negative in (False, True) for seed in (1, 7, 19)]
    if sources != source_hashes():
        raise RuntimeError("source changed during probe; reject mixed-source receipt")
    result = dict(limits="Synthetic serialized checkpoint fixture, not Orbital GC or capacity evidence; "
                  "hex bytes and synthetic charged working envelope, no native aliasing; "
                  "bounded listing and atomic deletion supplied by probe-only adapter.",
                  source_sha256=sources, storage_rows=rows, borrower_rows=borrowers)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(storage_histories=len(rows), borrower_histories=len(borrowers),
                         storage_errors=[len(row["errors"]) for row in rows],
                         borrower_errors=[len(row["errors"]) for row in borrowers])))


if __name__ == "__main__":
    main()
