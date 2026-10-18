# Using Ikea's hash table

Use the hash table for unsigned integer sets or maps whose values have a fixed
byte size. It supports every key width from 1 through 64 bits, including widths
such as 24 or 40 that do not match a C++ integer type. Values are byte arrays;
the enclosing representation owns their meaning and any referenced objects.
An owning multimap is not part of the graduated API.

Include `<ikea/hashtable.h>` and link `ikea::hashtable`. The target brings in
SeriesPack and TuplePack. The [rationale](rationale.md) explains the packed
buckets and overflow table; this guide focuses on building a correct, fast
consumer. Runnable starting points are
[ordinary operations](../../examples/hashtable/ordinary.cpp),
[native composition](../../examples/hashtable/composition.cpp) and
[accumulation](../../examples/hashtable/accumulation.cpp).

For an existing consumer, jump to [native hot loops](#bind-once-around-a-complete-hot-loop),
[prefetching and staged reads](#overlap-independent-requests),
[RMW](#update-without-a-second-lookup), [growth](#grow-by-replacing-the-owner),
[worker accumulation](#mutable-accumulation) or
[retained versions and byte effects](#integrate-retained-versions-and-byte-effects).

## Start with a concrete owner

`table<W, b, V, E, Kernel, Storage>` fixes the physical specialization at compile
time. After a reversible key permutation, the bucket address accounts for `b`
bits; the remaining `W − b` bits form the stored **quotient**. The first three
parameters are usually enough:

| Parameter | Meaning | Example |
| --- | --- | --- |
| `W` | Meaningful key bits; keys must be in `[0, 2^W)` | 24 |
| `b` | Log₂ of the number of primary buckets, from zero through `W` | 10 gives 1,024 buckets |
| `V` | Value bytes; zero selects a set | 8 |
| `E` | Physical quotient width; defaults to `W − b` | 14 |
| `Kernel` | Construction-time search recipe; defaults to `generic` | Selected by a measured profile when available |
| `Storage` | Backing allocation policy; defaults to aligned heap storage | Caller adapter when integrating another allocator |

The table owns its buffers. Its primary geometry is fixed for its lifetime;
ordinary insertions may allocate or rehash overflow, but do not grow primary
storage. Operations are synchronous. Keep reads coherent and exclude competing
mutation for each complete operation.

```cpp
#include <ikea/hashtable.h>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace ht = ikea::hashtable;

int main() {
    constexpr std::uint64_t seed = 713; // Example only: supplied by the caller.
    ht::table<24, 10, 0> keys(seed);
    const bool fresh = keys.insert(42);
    const bool duplicate = keys.insert(42);
    const bool present = keys.find(42, [](auto) {}); // Sets use the boolean result.
    assert(fresh && !duplicate && present);

    ht::table<24, 10, 8> counts(seed);
    ht::bytes<8> initial{};
    std::uint64_t n = 99;
    std::memcpy(initial.data(), &n, sizeof(n));
    const bool inserted = counts.insert(42, initial);
    assert(inserted);

    n = 0;
    const bool found = counts.find(42, [&](const auto& value) {
        std::memcpy(&n, value.data(), sizeof(n));
    });
    assert(found && n == 99);
    const bool erased = counts.erase(42);
    const bool erased_again = counts.erase(42);
    assert(erased && !erased_again);
}
```

`memcpy` avoids alignment and aliasing assumptions about byte values; choose
an explicit byte order if values cross a representation or persistence boundary.

### Know the operation's result

| Operation | Result and callback behavior |
| --- | --- |
| `insert(key, value)` | Upsert; returns true for a new key, false for replacement |
| `find(key, consume)` | Returns membership; for maps invokes `consume(const bytes<V>&)` once on a hit and never on a miss |
| `modify(key, update)` | Maps only; returns membership, updating once on a hit |
| `modify_or_insert(key, initial, update)` | Maps only; returns true for insertion; invokes `update(value, present)` once |
| `erase(key)` | Returns true if an entry was removed |
| `visit(visitor)` | Calls `visitor(key, value)` for every entry; supplies an empty byte array for sets |
| `clone(context)` | Explicit independent copy of backing and contents |
| `validate()` | Expensive consistency check for diagnostics |

A set lookup's consumer is unused: use the returned boolean. Iteration order is
physical and can change after rebuilding. Map lookup values are temporary
complete values, even when physical bytes are striped; copy what you need
inside the callback. Do not retain references to callback values or treat a
reported location as a stable entry handle.

Ordinary writes validate keys. Concrete/native reads, including `prepare`,
`find` and staged reads, trust the `W`-bit domain; admitting untrusted keys is
the caller's job. `descriptor::read().find` validates it, and guarded reads
report invalid keys as `denied`.

## Choose geometry and native execution together

For `K = W − b > 9`, primary capacity is `2^b × N`, with `N` determined by
the [geometry law](rationale.md#fit-capacity-first-then-choose-an-encoding).
It is a nominal primary row count, not an insertion limit: bucket imbalance
creates spills before it is full. For `K ≤ 9`, presence bitmaps cover the entire
key domain without overflow. A map reserves value rows even for absent keys,
so dense-domain bitmaps can be expensive for large values.

Start with enough buckets for the expected population and monitor both `size()`
and `spill_size()`. `capacity()` stays fixed. Growth is a caller decision; the
descriptor's `needs_growth(850)` is an optional 850-per-mille occupancy test,
not an automatically enforced growth policy. Seed selection and coordination
across worker tables also belong to the caller.

The generic encoding is a portable starting point. For a measured deployment,
`descriptor<W,V>::measured<b>(seed, cpu, isa)` chooses an encoding and kernel
at construction. Current profiles cover sets and values of 8 or 64 bytes:

| CPU name | ISA names |
| --- | --- |
| `zen5`, `granite-rapids` | `avx2`, `avx2-gfni`, `avx512` |
| `neoverse-v2` | `neon` |

Choose a profile compatible with the actual build flags and machine. This API
does not discover CPU features. For `K > 9`, an unknown CPU/ISA/value-size
combination refuses selection rather than borrowing another target's results;
use explicit encoding for an unprofiled surface. Bitmap geometries need no
profile. The [profile provenance](../../../workbench/benchmarks/hashtable/profiles/provenance.json)
records what each selection measured; it is not an all-operation optimum.

Use [Building](../../../BUILDING.md) for the pinned toolchain and independent
ISA/tuning flags. Specialized recipes have exact fallbacks when their ISA is
absent, but that does not reproduce their measured performance.

## Bind once around a complete hot loop

A concrete owner exposes inline operations directly. A `descriptor<W,V,Storage>`
holds a runtime-selected concrete owner and operation table, making replacement
and ordinary point calls convenient. For a repeated loop, capture a typed reader
once and borrow its one-pointer native view:

```cpp
#include <ikea/hashtable.h>
#include <array>
#include <cassert>

namespace ht = ikea::hashtable;

int main() {
    auto values = ht::descriptor<24, 8>::explicit_encoding<10, 14>(713);
    ht::bytes<8> payload{};
    payload[0] = 7;
    for (auto key : {11u, 22u, 33u})
        values.insert(key, payload);

    auto held = values.read_native<10, 14>();
    auto view = held.borrow();
    const std::array<std::uint64_t, 4> requests{11, 44, 22, 33};
    unsigned sum = 0;
    for (auto key : requests)
        view.find(key, [&](const auto& value) { sum += value[0]; });
    assert(sum == 21);
}
```

The specialization must match the descriptor's current `b`, `E` and kernel;
a mismatch fails at binding. A capture retains its owner and the storage
policy's retention tokens. Keep it alive until every borrowed view and pending
probe has finished. For runtime profile selection, `bind_measured_encoding<K>`
supplies encoding and kernel as compile-time constants to an enclosing callback;
put the complete native consumer inside that callback.

Use `read()` when an erased point reader is sufficient. Avoid repeatedly
capturing shared ownership or crossing an erased callback per key in a loop
that can use one native binding. Descriptor overhead is not included in
concrete-owner benchmark timings.

### Overlap independent requests

For one independent prefetch stage, prepare several concrete-owner probes,
prefetch them, perform other useful work, then call `finish(probe, same_key,
consume)`. A probe belongs to that owner, key and coherent backing. Keep all
three stable through completion. `prefetch` only issues a hint; it does not wait.

For dependent accesses, use a native view or concrete owner with
`prepare_staged(state, key, emit)` and `advance(state, found, consume, emit)`:

1. `prepare_staged` resets the state and emits the primary key region.
2. The caller prefetches emitted extents and can run other probes.
3. `advance` returns **true when done**, with `found` indicating membership.
   A false return emits the next dependent extent and requires another advance.
4. Keep the state, owner, capture, key and consumer state until completion.

`emit(pointer, bytes)` is a description of bytes to access. A prefetch consumer
should cover every cache line intersecting that extent; emitting one extent
does not imply one cache line. No callback is allowed to invalidate the owner.
The [dependency diagram](rationale.md#specialize-complete-consumers-and-expose-memory-dependencies)
shows primary-value and Swiss-control/tuple stages. The API itself never yields
or waits.

For eight-byte values, `held.borrow<staged_values::lookahead8>()` schedules
all primary value lines alongside the key region. It can remove a later visit
on a hit, at the cost of traffic for unused rows and misses. Keep the default
dependent scheduling until a complete consumer demonstrates a benefit.
Heat and prefetch policy remain caller-held; see the
[Loom stream guide](../../../loom/docs/stream.md) for scheduling interleaved
work. Measure serial latency separately from throughput with independent work.

## Update without a second lookup

Use `modify` for an existing-key computation and `modify_or_insert` when absence
starts from an initial value. The updater receives a temporary full value.
For upsert it also receives `present`: true means the value came from the table,
false means it came from `initial`. Its return value is unused.

```cpp
#include <ikea/hashtable.h>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace ht = ikea::hashtable;

int main() {
    ht::table<24, 10, 8> counts(713);
    auto increment = [](auto& value) {
        std::uint64_t n;
        std::memcpy(&n, value.data(), sizeof(n));
        ++n;
        std::memcpy(value.data(), &n, sizeof(n));
    };
    const bool inserted = counts.modify_or_insert(
        42, ht::bytes<8>{}, [&](auto& value, bool) { increment(value); });
    assert(inserted);
    const bool modified = counts.modify(42, increment);
    assert(modified);
    std::uint64_t n = 0;
    counts.find(42, [&](const auto& value) { std::memcpy(&n, value.data(), sizeof(n)); });
    assert(n == 2);
}
```

An existing-only miss does not invoke the updater. An updater must not reenter,
move or invalidate the owner, and its value reference must not escape. If it
throws, logical contents are unchanged. Ordinary `modify_or_insert` may already
have prepared extra overflow capacity. Side effects on a caller-owned append
buffer or another object need their own failure and publication policy; updating
a reference here is not a transaction across both objects.

For replacement values already computed, choose the insertion strategy for
the traffic. `insert<write_strategy::mixed>` is the default, combining primary
insertion and replacement stores. `insert<write_strategy::existing>` avoids
key/flag stores on a hit and still inserts missing keys. Neither name is a
promise that keys exist. The [rationale](rationale.md#mutations-expose-their-physical-effects-before-writing)
explains the shared-byte cost and guarded alternatives.

A descriptor stores the strategy selected at construction. For a hot mutable
batch, `with_native_mutation<b,E,Kernel>(callback)` supplies a concrete owner
and a compile-time strategy constant. Use
`owner.template insert<decltype(writer)::value>(key, value)` to preserve that
selection. The [composition example](../../examples/hashtable/composition.cpp)
shows this alongside RMW. This is a synchronous borrowed binding: exclude
concurrent access, keep the descriptor and owner in place, and let no reference
escape. It acquires no reader retention tokens that would obstruct an internal
Swiss rehash.

## Grow by replacing the owner

Concrete growth names a destination with one more bucket bit:

```cpp
#include <ikea/hashtable.h>
#include <cassert>

namespace ht = ikea::hashtable;

int main() {
    ht::table<24, 10, 8, 14> source(713);
    source.insert(42, {});
    using Larger = ht::table<24, 11, 8, 14>;
    auto grown = ht::grow<Larger>(source);
    assert(grown->size() == source.size());
    grown->erase(42);
    assert(source.find(42, [](auto) {}));
}
```

`grow` builds an independent same-seed destination; allocation failure preserves
the source. Its geometry must admit at least the old row positions, or be the
bitmap transition. With a descriptor, `grow<b,E,Kernel>()` verifies the current
specialization, constructs the next owner, then replaces it. By default it keeps
the encoding/kernel when compatible, otherwise choosing exact generic encoding.
Explicit `NextE` and `NextKernel` can select the destination. General
`rebuild_explicit` or `rebuild_measured` instead reconstructs entries for the
requested bucket count and encoding.

Retained captures continue using the old owner after descriptor replacement.
They do **not** hold a content snapshot of a backing modified in place. Finish
pending reads before mutating that backing. Before replacing buffers in place
(for example, through Swiss growth or `reserve_overflow`), also release captures
retaining those buffers; an in-place rehash does not rebind their retention
tokens. If readers need an older version, edit an independent owner and publish
it through the enclosing system. A custom storage context must outlive the
descriptor, escaped captures and final token release. The descriptor is not a
concurrent root or an MVCC implementation.

## Mutable accumulation

Use `accumulate(destination, source, reducer)` to fold worker tables into
exclusive backing. Key width and value type must match, seeds must agree,
and source and destination must not alias. Source and destination geometries
and encodings may differ.

Choose destination geometry, then call `reserve_overflow(destination, bound)`
before accumulation. `bound` is an upper bound on its **final live spill count**,
at least its current spill population. Existing population plus all incoming
populations is safe but can over-reserve heavily; a tighter application bound
saves memory. Primary capacity and Swiss reservation are separate decisions.

The [runnable example](../../examples/hashtable/accumulation.cpp) starts with
an empty larger table, reserves backing, then adds two workers in order. Its
core loop is:

```cpp
// result is exclusive, fully sized and already reserved; sources stay coherent.
for (const auto* source : {&first, &second}) {
    if (ht::accumulate(result, *source, add) != ht::accumulation_status::complete)
        return 1; // Discard result; it may already contain partial work.
}
```

New keys copy their values. On a duplicate, `reduce(current, incoming)` returns
the replacement value. The initial destination contributes first, followed by
each source in caller order. The reducer need not be associative. A set can
omit it. Do not reenter or invalidate either owner from a reducer.

The accumulation operation keeps table backing fixed and neither obtains grants
nor publishes damage. It returns `complete`, `incompatible`, `alias` or
`needs_overflow`; discard the destination after **any** non-complete result or
exception. Sources remain unchanged. Only
a successfully completed destination is eligible for final publication.

To preserve an initial input, `clone` it explicitly, then reserve and accumulate.
To reuse a disposable worker allocation, transfer that owner instead. A descriptor
offers the same operations with explicit source/destination specializations.
Preparation and leaf time are separate in the
[accumulation measurements](../../../workbench/benchmarks/hashtable/ACCUMULATION.md).

Worker-local tables with coordinated seeds are a natural way to use multiple
writers. Route disjoint keys to their owners, or fold overlapping owners with
an ordered reducer. This API supplies no concurrent insertion, worker routing,
balancing or coordinated-growth protocol. Distinct key locks alone do not
protect shared packed bytes, global counts and Swiss metadata.

## Integrate retained versions and byte effects

Use the concrete owner's `guarded_find`, `guarded_insert`, `guarded_erase`,
`guarded_modify` and `guarded_modify_or_insert` when the enclosing representation
must authorize access and preserve or journal bytes. Supply a context with:

| Hook | Required behavior |
| --- | --- |
| `enter(owner, key, intent) -> bool` | Establish stable readable storage or private/exclusively writable storage, retaining permission through the call |
| `prepare(span<mutation_damage>) -> bool` | Resolve live sources/generations and reserve preservation or journal capacity for **all** spans; refusal leaves no published effects |
| `before(source, byte_write) noexcept` | Publish the prepared byte effect before stores; cannot invalidate sources or fail |

Each damage item identifies a live owner part, base and extent, plus an
offset/length span. Resolve it to the enclosing object's stable incarnation,
part and generation while valid. Do not persist the raw pointers as identities.
The same context must provide the physical protection required by its readers;
the hooks are not locks or snapshots by themselves.

Guarded operations do not allocate table backing or rehash overflow.
`needs_capacity` means prepare capacity under exclusive/private ownership,
then obtain fresh admission and retry. A guarded write can return `denied`,
`absent`, `inserted`, `updated`, `unchanged` or `erased` as appropriate.
Guarded reads return `found`, `absent` or `denied`. Invalid keys are denied.

Guarded RMW invokes no updater on absence for `guarded_modify`, refusal or
insufficient capacity. A successful preparation precedes the updater; damage
publication and live stores follow it. If the updater throws, prepared resources
must unwind safely. Existing-value RMW publishes the value's physical spans;
guarded insertion/deletion may conservatively cover a whole primary bucket.
There is no automatic comparison of old and new values: a successful map update
can report `updated` even if its bytes are equal.

The [guarded histories](../../test/hashtable/guarded-check.cpp) and
[RMW checks](../../test/hashtable/modify-check.cpp) show refusal, damage and
exception behavior. The [owner integration guide](../integration.md) explains
how these bounded calls fit within a larger representation's visibility and
lifetime rules.

### Account for allocation separately from contents

`bytes_used()` reports buffer capacities plus the concrete owner object; it
is not process RSS or the full cost of a descriptor's shared ownership.
`storage()` separates assigned bytes, accessible bytes and reserved bytes as
reported by the storage policy. `allocations(visitor)` exposes concrete backing
extents for integration. A table has a primary allocation and optional Swiss
controls and tuples.

The default heap policy frees backing when destroyed. Its tail trim only changes
the assigned extent; it does not return heap capacity. A custom policy can use
reservation and live-tail reclamation, respecting its retention tokens.
`relinquish_surplus()` asks each buffer to release surplus and reports failure;
it does not compact table entries or shrink the primary geometry.

An allocation adapter implements the buffer/context/retention contract in
[storage.h](../../include/ikea/hashtable/storage.h), including nonthrowing buffer
moves after allocation preflight. It covers packed backing, not every STL
metadata or input allocation in the surrounding consumer. Keep allocator and
representation ownership policy at that integration boundary.

## Validate the complete consumer

Build and run the examples with the [pinned Linux toolchain](../../../BUILDING.md).
From this macOS/OrbStack workspace, prefix Linux commands with `orb -m ubuntu`:

```sh
cmake --preset dev
cmake --build --preset dev --target ikea_example_hashtable_ordinary \
  ikea_example_hashtable_composition ikea_example_hashtable_accumulation -j2
ctest --test-dir build/clang/dev --output-on-failure -R '^ikea_example_hashtable_'
```

`ikea_hashtable_validate` additionally builds and runs the module checks. For
performance, use the [maintained comparison suite](../../../workbench/benchmarks/hashtable/README.md)
and time the complete loop with its real value consumption, preparation,
occupancy and allocator. Keep dependent latency, independent throughput and
memory accounting distinct. Wider encodings, lookahead, writer selection and
prefetching each have measured losing cases; select them at a complete-operation
boundary and measure the workload they are meant to improve.
