"""Finite shuffle lifetimes; synthetic byte/tick model, not a runtime or IO model."""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import heapq
import json
import math
from pathlib import Path


@dataclass(frozen=True)
class Case:
    name: str
    policy: str  # ram, split, spool, replay, admit
    inputs: int = 12
    output_bytes_per_input: int = 128
    grain_bytes: int = 128
    input_bytes: int = 16
    memory_bytes: int = 1024
    active_reserve_bytes: int = 144
    spool_bytes: int = 4096
    source_bytes: int = 4096
    source_replayable: bool = True
    compute_ticks_per_byte: float = 0.125
    task_ticks: float = 1.0
    io_ticks_per_byte: float = 0.05
    io_fixed_ticks: float = 2.0
    publication_delay: float = 10.0
    cancel_at: float | None = None
    resolution_delay: float = 20.0
    horizon: float = 10000.0


def run(case: Case) -> dict:
    """One producer/consumer region, one serial spool writer, no network.

    Each input i emits output_bytes_per_input exact unit records of value i+1.
    The private reducer consumes each produced chunk immediately. Selected
    replay policy keeps the declared immutable source through resolution; other
    policies additionally retain every intermediate until resolution.
    Chunk construction reserves input scratch and complete output atomically.
    All source bytes are in a distinct finite durable store, charged below.
    """
    if case.policy not in {"ram", "split", "spool", "replay", "admit"}:
        raise ValueError("unknown policy")
    if min(case.inputs, case.output_bytes_per_input, case.grain_bytes,
           case.input_bytes, case.memory_bytes) <= 0:
        raise ValueError("positive workload and memory required")
    if case.active_reserve_bytes < 0 or case.spool_bytes < 0 or case.source_bytes < 0:
        raise ValueError("capacities must be nonnegative")
    durations = (case.compute_ticks_per_byte, case.task_ticks, case.io_ticks_per_byte,
                 case.io_fixed_ticks, case.publication_delay, case.resolution_delay,
                 case.horizon, *(() if case.cancel_at is None else (case.cancel_at,)))
    if any(not math.isfinite(value) or value < 0 for value in durations):
        raise ValueError("timing inputs must be finite and nonnegative")
    chunks = [(i, start, min(case.grain_bytes, case.output_bytes_per_input - start))
              for i in range(case.inputs)
              for start in range(0, case.output_bytes_per_input, case.grain_bytes)]
    total_output = case.inputs * case.output_bytes_per_input
    source_pin = case.inputs * case.input_bytes
    expected = sum(range(1, case.inputs + 1)) * case.output_bytes_per_input
    result = {
        "case": asdict(case), "offered_inputs": case.inputs,
        "offered_chunks": len(chunks), "offered_output_bytes": total_output,
        "expected_sum": expected,
    }
    # These are explicit preflight failures before any execution/effect.
    rejection = None
    if source_pin > case.source_bytes:
        rejection = "source retention does not fit"
    if case.policy == "replay" and not case.source_replayable:
        rejection = "source/code not declared replayable"
    if case.policy == "admit" and total_output + case.input_bytes > case.memory_bytes:
        rejection = "declared full output bound does not fit"
    if rejection:
        return result | {"status": "rejected", "reason": rejection,
                         "consumed_chunks": 0, "unfinished_chunks": len(chunks),
                         "published": False, "sum": 0, "cpu_ticks": 0.0,
                         "cpu_scheduled_ticks": 0.0,
                         "spool_write_bytes": 0, "peak_memory_bytes": 0,
                         "peak_source_pin_bytes": 0, "peak_spool_bytes": 0,
                         "retired_at": 0.0, "trace": []}

    events: list[tuple[float, int, str, object]] = []
    sequence = 0
    now = 0.0
    memory = retained_ram = spool_used = 0
    active_cpu = active_io = False
    cpu_reservation = 0
    io_wait: list[int] = []
    next_chunk = consumed = accumulator = durable_chunks = 0
    peak_memory = peak_spool = 0
    cpu_ticks = cpu_scheduled_ticks = write_bytes = 0
    memory_byte_ticks = source_byte_ticks = spool_byte_ticks = 0.0
    cancelled = abort_known = published = resolved = False
    retired_at = publication_at = None
    cancellation_seen = None
    horizon_reached = False
    trace: list[dict] = []

    def record(event: str, **fields) -> None:
        trace.append({"t": round(now, 6), "event": event,
                      "memory": memory, "retained_ram": retained_ram,
                      "spool": spool_used, **fields})

    def schedule(delay: float, kind: str, payload=None) -> None:
        nonlocal sequence
        sequence += 1
        heapq.heappush(events, (now + delay, sequence, kind, payload))

    def observe() -> None:
        nonlocal peak_memory, peak_spool
        assert 0 <= memory <= case.memory_bytes
        assert 0 <= retained_ram <= memory
        assert 0 <= spool_used <= case.spool_bytes
        peak_memory = max(peak_memory, memory)
        peak_spool = max(peak_spool, spool_used)

    def retire_if_possible() -> None:
        nonlocal memory, retained_ram, spool_used, retired_at, resolved
        if (published or (cancelled and abort_known)) and not active_cpu and not active_io:
            memory = retained_ram = spool_used = 0
            io_wait.clear()
            retired_at = now
            resolved = True
            record("retired")

    def drive() -> None:
        nonlocal next_chunk, memory, active_cpu, active_io, cpu_scheduled_ticks
        nonlocal spool_used, cpu_reservation
        retire_if_possible()
        if resolved:
            return
        # Once cancelled, existing CPU/IO users must stop before reclamation.
        if cancelled:
            return
        if case.policy == "spool" and io_wait and not active_io:
            size = io_wait[0]
            if spool_used + size <= case.spool_bytes:
                io_wait.pop(0)
                spool_used += size  # reserve disk space before write starts
                active_io = True
                schedule(case.io_fixed_ticks + size * case.io_ticks_per_byte, "io", size)
                record("spool_start", bytes=size)
        if not active_cpu and next_chunk < len(chunks):
            item = chunks[next_chunk]
            size = item[2]
            reserve = case.input_bytes + size
            fits = memory + reserve <= case.memory_bytes
            if case.policy == "split":
                # Logical partitions share the same physical memory budget.
                fits &= reserve <= case.active_reserve_bytes
                fits &= retained_ram + size <= case.memory_bytes - case.active_reserve_bytes
            if fits:
                next_chunk += 1
                active_cpu = True
                cpu_reservation = reserve
                memory += reserve
                duration = case.task_ticks + size * case.compute_ticks_per_byte
                cpu_scheduled_ticks += duration
                schedule(duration, "cpu", item)
                record("compute_start", input=item[0], offset=item[1], bytes=size)
        if (consumed == len(chunks) and not active_cpu and not active_io and not io_wait
                and not any(event[2] == "publish" for event in events)):
            schedule(case.publication_delay, "publish")
            record("ready_for_decision")
        observe()

    if case.cancel_at is not None:
        schedule(case.cancel_at, "cancel")
    drive()
    last_time = 0.0
    while events and not resolved:
        event_time, _, kind, payload = heapq.heappop(events)
        if event_time > case.horizon:
            horizon_reached = True
            break
        now = event_time
        elapsed = now - last_time
        cpu_ticks += elapsed if active_cpu else 0
        memory_byte_ticks += memory * elapsed
        source_byte_ticks += source_pin * elapsed
        spool_byte_ticks += spool_used * elapsed
        last_time = now
        if kind == "cpu":
            active_cpu = False
            i, _, size = payload
            memory -= cpu_reservation
            cpu_reservation = 0
            if not cancelled:
                consumed += 1
                accumulator += (i + 1) * size
                if case.policy != "replay":
                    memory += size
                    retained_ram += size
                if case.policy == "spool":
                    io_wait.append(size)
                record("consume", input=i, bytes=size)
            else:
                record("compute_stopped", input=i, bytes=size)
        elif kind == "io":
            active_io = False
            memory -= payload
            retained_ram -= payload
            write_bytes += payload
            durable_chunks += 1
            record("spool_durable", bytes=payload)
        elif kind == "cancel":
            if published:
                record("cancel_after_commit")
            else:
                cancelled = True
                cancellation_seen = now
                schedule(case.resolution_delay, "abort")
                record("cancel_requested")
        elif kind == "abort":
            abort_known = True
            record("abort_known")
        elif kind == "publish":
            if not cancelled:
                assert accumulator == expected and consumed == len(chunks)
                assert case.policy != "spool" or durable_chunks == len(chunks)
                published = True
                publication_at = now
                record("published")
        else:
            raise AssertionError(kind)
        observe()
        drive()

    status = "published" if published else "cancelled" if resolved else "stalled"
    if not resolved:
        cpu_ticks += case.horizon - last_time if active_cpu else 0
        memory_byte_ticks += memory * (case.horizon - last_time)
        source_byte_ticks += source_pin * (case.horizon - last_time)
        spool_byte_ticks += spool_used * (case.horizon - last_time)
    result.update(
        status=status, halt_reason="horizon" if horizon_reached else "resolved" if resolved else "resource_wait",
        consumed_chunks=consumed, unfinished_chunks=len(chunks) - consumed,
        sum=accumulator, published=published, publication_at=publication_at,
        cancellation_at=cancellation_seen, retired_at=retired_at,
        cpu_ticks=cpu_ticks, cpu_scheduled_ticks=cpu_scheduled_ticks, spool_write_bytes=write_bytes,
        source_read_bytes=next_chunk * case.input_bytes,
        peak_memory_bytes=peak_memory, peak_source_pin_bytes=source_pin,
        peak_spool_bytes=peak_spool, outstanding_memory_bytes=memory,
        outstanding_spool_bytes=spool_used, outstanding_source_pin_bytes=0 if resolved else source_pin,
        memory_byte_ticks=memory_byte_ticks, source_byte_ticks=source_byte_ticks,
        spool_byte_ticks=spool_byte_ticks, trace=trace)
    return result


def cases() -> list[Case]:
    result = [Case(f"retained-{policy}", policy)
              for policy in ("ram", "split", "spool", "replay", "admit")]
    result += [
        Case("small-ram", "ram", inputs=4),
        Case("spool-too-small", "spool", spool_bytes=512),
        Case("spool-exact", "spool", spool_bytes=1536),
        Case("source-too-small", "replay", source_bytes=128),
        Case("source-not-replayable", "replay", source_replayable=False),
        Case("wide-output", "replay", output_bytes_per_input=512,
             grain_bytes=512, memory_bytes=256),
        Case("chunked-output", "replay", output_bytes_per_input=512,
             grain_bytes=32, memory_bytes=256),
        Case("chunked-spool", "spool", output_bytes_per_input=512,
             grain_bytes=32, memory_bytes=256, spool_bytes=8192),
        Case("slow-spool", "spool", io_ticks_per_byte=1.0),
        Case("long-publication-ram", "ram", inputs=4, publication_delay=1000),
        Case("long-publication-replay", "replay", inputs=4, publication_delay=1000),
        Case("cancel-large-grain", "replay", grain_bytes=128, cancel_at=5, resolution_delay=2),
        Case("cancel-small-grain", "replay", grain_bytes=16, cancel_at=5, resolution_delay=2),
        Case("cancel-slow-io", "spool", io_ticks_per_byte=1.0, cancel_at=20, resolution_delay=2),
        Case("cancel-slow-resolution", "ram", cancel_at=20, resolution_delay=1000),
        Case("cancel-stalled", "ram", cancel_at=500, resolution_delay=2),
    ]
    return result


def retained_result(case: Case) -> dict:
    result = run(case)
    trace = result["trace"]
    result["trace_event_count"] = len(trace)
    result["full_trace_sha256"] = hashlib.sha256(
        json.dumps(trace, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    # Repeated chunk events are regenerable; keep boundaries and all lifecycle events.
    indices = set(range(min(8, len(trace)))) | set(range(max(0, len(trace) - 8), len(trace)))
    indices |= {i for i, entry in enumerate(trace) if entry["event"] in {
        "ready_for_decision", "published", "retired", "cancel_requested", "abort_known", "compute_stopped"}}
    result["trace"] = [trace[i] for i in sorted(indices)]
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = Path(__file__)
    report = {"model": "synthetic bytes/ticks; all cases selected, including stalls",
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "trace_selection": "first/last eight events and all lifecycle events; full trace hash retained",
              "cases": [retained_result(case) for case in cases()]}
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
