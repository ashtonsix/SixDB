# Live-tail allocation architecture

The spike implements **owner relinquishment followed by allocation maintenance**.
The owner knows its useful planes; allocation knows physical users, shared pages,
pending work and budget. The [spike README](README.md) owns results and selection;
this note explains the mechanism and alternatives. Native performance qualification
remains separate from the local diagnostic runs.

## One caller contract, existing backends

`relinquish_tail(allocation, bytes, padding)` contracts the writer's access without
moving storage, initializing bytes or issuing VM work. It also permits zero extent.
The caller ends overlapping raw borrows and includes every still-used sidecar and
native readable footprint. A schema ceiling, allocated capacity or damage journal
cannot identify that boundary for it.

`trim_cold` later selects whole eligible pages and settles them asynchronously.
Groups keep packing/shared-page occupancy; Windows keep a contiguous reservation
and accessible prefix. Pool/Arena keep the existing budget and helper/owner delivery.
A Group cannot return a page overlapping any occupied home; shrinking its logical
body does not change its physical class or invent a sparse-allocation interface.
Small allocations therefore often have no independently returnable tail page.

Keep the quantities separate: writer body/padding, historical reader endpoints,
accessible capacity, reserved VA and observed residency. The safe prefix includes
both writer and reader access, rounded up to the actual native base page. A high
sidecar or a live prefix within one 64 KiB base page can eliminate the saving.
Fresh allocation with small body and large reserve already avoids preparing the
whole reservation; it does not reclaim previously touched oversized reused backing.

## Bounded reader endpoints

The selected implementation lazily allocates a charged array of atomic counters
on the first real Window Hold. It leaves Hold and the four-byte Storage unchanged.
For endpoint `end`, native page `P` and reservation `V`:

```text
bin = bit_width((end - 1) / P)
protected upper bound = min(V, P * 2^bin)
```

It records the upper endpoint of arbitrary offset/length footprints, not their
initialized value count. A five-page footprint conservatively protects eight pages;
a one-page footprint protects one. Retiring the wide reader lowers the bound even
while narrow readers overlap continuously. Retaining an old Hold increments its
same bin. Foreign release decrements the bin before dropping its physical use;
owner trim reads counters with acquire ordering. Do not use `users == 1` as a
general proof of exclusive mutable ownership.

Physical capture/export tokens with no footprint skip the counters. Their storage
remains protected by the writer extent retained in the Window after capture or
Allocation release. Ordinary growth preserves that bound, including historical
padding; only explicit relinquishment contracts it. Real earlier Holds independently
protect their old footprints. This is conservative for captured subsets rather than
an exact per-Part reclaim policy. Shared ownership alone still proves no address
stability or permission to mutate captured bytes.

No per-Hold registry, movable intrusive links or reader-cohort API is introduced.
The array remains with its Window and is refunded with its metadata. Native reads
still use an ordinary pointer. Every real Window Hold creation/release gains counter work,
even without reclamation;
first Hold can refuse if its metadata cannot be allocated/charged. That cost is
measured rather than described as free when no tail has been relinquished.

## Settlement and later use

Before dispatch, trim freezes its suffix and withdraws it from accessible capacity.
Growth into that range refuses until the owner processes the completion reply;
prefix-only access can continue. The helper batches `MADV_DONTNEED` and `PROT_NONE`, retaining VA. The owner
refunds confirmed backing only after handling the reply. Failed or uncertain work
keeps its conservative charge and exclusion until repair or restored access.
Generation checks after a destructive syscall cannot rescue a racing writer.

Cancellation/release must not shorten accepted physical work. Existing Arena pins,
backend records, owner replies and close handle that lifetime; no parallel whole
Window retirement may reuse the same record. Later growth reacquires budget before
restoring access, preserves useful bytes and leaves initialization to the writer.
`MADV_FREE` eligibility alone would not justify a backing refund.

There is no deferred request waiting for all readers to end. `trim_cold` concerns
currently eligible work; revisit it after wider readers retire. This avoids assuming
a notification that does not exist: a foreign last-Hold decrement with a live
Allocation does not emit today's final-Storage-return Message. Maintenance policy
must account for that opportunistic progress contract.

## Why this comparison

The [older prototypes](../memory-descriptors/VIRTUAL.md#growth-and-readers) explored
both maximum-until-all-grants-end and individual endpoint tracking. The former can
retain an obsolete wide bound indefinitely under overlapping narrow readers;
cohorts can defer to their remaining old narrow readers. Exact endpoints remove
bin rounding but add registration, moves, foreign removal and maximum maintenance.
Their earlier evidence is serialized correctness, not a concurrent production cost.
Binned counts test a bounded middle ground. Quiescent-only reclaim is a useful
control, but does not meet the requirement to preserve narrow outstanding Holds.
Copying useful material remains valid, with changed address and overlap cost.

[Pure growth](../orbital-mvcc/metadata/orbital/allocation/README.md#further-treatments)
removed universal held-extent bookkeeping and implicit initialization. This change
must preserve that separation and show its complete price. `bench.cpp` compares
unchanged-source common paths, finite and Window Hold cycles, foreground contraction,
actual asynchronous settlement and regrowth/first-write faults. Its watermark/cohort
rows count this trace's retained pages; they do not time alternative implementations.
The local runs demonstrate stable VA/prefix, old wide reads, retirement under live
narrow Holds, backing refunds and zero-charge close. Paired native trials determine
ordinary-path cost; first-Hold latency and optional metadata are reported separately.

Use existing [padding/Hold regressions](../../../orbital/test/allocation.cpp),
[foreign lifetime histories](../../../orbital/test/lifetime.cpp) and
[paused reclamation](../../../orbital/test/reclamation.cpp) to qualify the mechanism.
The [slack diagnostic](../allocation-reuse-temperature/slack/README.md) establishes
reclaim/refault trade-offs; the [TLB study](../orbital-views/tlb/README.md) explains why
helper execution still disturbs same-process workers. An asynchronous syscall is
neither free foreground capacity nor a cache-residency promise.
