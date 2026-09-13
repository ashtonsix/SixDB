# Using Bec256

Bec256 (Bisection–Enumerative Code) encodes a 32-byte bitset of 256 positions
into a body of at most 47 bytes.
The caller owns the storage and retains each body's **population** (number of set
bits), exact byte length and address. Reading requires that association because
the body contains no header. The [representation guide](representation.md)
explains the count tree, byte ranks and resulting wire format.

Include `<ikea/bec256.h>` and link `ikea::bec256`. Encode into caller storage,
retain the returned length with the population, admit a source for reading,
and decode it when plain bits are needed.

## Encode a block and read it back

A named `destination` describes writable storage; a write journal records the
operation's stores for the owner's storage protocol. The [ordinary example](../../examples/bec256/ordinary.cpp)
sets position 29 and places its one-byte body at offset 17:

```cpp
namespace bc = ikea::bec256;
bc::plain_block bits{};
bits[3] = bc::byte{0x20};
std::array<bc::byte, 128> storage{};
bc::destination target{storage};
std::array<ikea::owner_write, 1> records;
ikea::source_write_journal effects{records};
auto bytes = bc::encode(bits, 1, target, 17, effects);
assert(bytes && *bytes == 1);
assert(storage[17] == bc::byte{0xa7});

auto source = bc::source::admit(
    std::span(storage).subspan(17, *bytes), *bytes, 1);
assert(source);
bc::plain_block decoded;
bc::decode(*source, decoded);
assert(decoded == bits);
```

Here the journal records one byte at offset 17, relative to `target.storage`,
with `&target` as its source identity and plane zero. Keep the named destination
alive until the owner resolves those effects to its storage identities. The
[representation walkthrough](representation.md#one-set-bit-locating-a-byte-then-a-bit-within-it)
derives the body `a7`.

`encode` checks that population matches the input and that destination and journal
capacity suffice before any store or effect. Rejection leaves both unchanged. It writes
the exact returned body length. All 256 input bits are consumed before the first
store, permitting input/destination byte overlap. Command objects and journal
storage remain disjoint as required by [codec.h](../../include/ikea/bec256/codec.h).

Examples assert success; applications should handle errors before dereferencing
results. Report `bc::describe(result.error())` with population, length and supplied
extents to diagnose rejection.

## Admit the body, then reuse its interpretation

`source::admit(storage, bytes, population)` validates the exact body prefix and
returns a borrowed source for repeated reads without another framing scan.
It rejects impossible populations, truncated fields, invalid splits or leaf ranks,
nonzero final padding bits and trailing body bytes. Another supplied population
can describe different valid content: admission does not authenticate the
population/length/address association. The owner keeps that association and the
bytes valid; writes or remapping can invalidate admission.

`decode` materializes exactly 32 plain bytes. `decode_pair`
materializes 64: its first half comes from its first argument, its second from
its second argument. Both inputs are decoded before any output store, so output
may overlap either body. Input addresses need not be adjacent.

An exact body span, as used above, is sufficient for admitted ordinary and native
reads. The [memory access bounds](#memory-access-bounds) below explain the optional
larger readable window and the different requirements of raw entries.

## Write immediately or retain a prepared candidate

Prepare a candidate when the owner must acquire space or coordinate several
writes before admitting a destination:

```cpp
auto candidate = bc::prepare(replacement, population);
assert(candidate);
// The owner may wait here.
auto admitted = candidate->admit_write(target, offset, effects);
assert(admitted);
candidate->write_unchecked(target, offset, effects);
```

`prepare` validates population and encodes without allocation or caller-storage
writes. Its `encoding` owns transient bytes that survive owner waits; `bytes()`
gives the exact storage need. `candidate->write(...)` combines admission and
writing when separate steps are unnecessary.

`admit_write` checks a proposed write; it reserves nothing. Two children can each
pass against the same last journal slot. The owner must budget the combined work
before either writes and retain stable geometry, command storage and proofs
through execution. A nonempty body requires one available journal entry; an empty
body issues no store or effect, though a checked write still validates its offset.

## Estimate space or skip an unpromising encode

`estimate_bytes(bits)` uses a fixed statistical model to predict headless body
bytes in 0..47, with zero for empty/full inputs. It is **not an allocation bound**;
use `prepare` and `bytes()` for an exact size. Estimates exclude owner metadata
and may have correlated errors across related blocks.

`encode_if_promising` compares a cutoff with `enum_bits(bits)`: the exact sum of
byte-rank widths, 0..224 **bits**, excluding the count tree. It does not consult
the size estimate. The result is an error, a decline (empty optional), or an
encoded byte count, possibly zero:

```cpp
auto attempt = bc::encode_if_promising(bits, population, cutoff, target, offset, effects);
if (!attempt) {
    // Handle a population or accepted-write admission error.
} else if (!*attempt) {
    // Heuristic decline: retry encoding or choose another representation.
    // No bytes/effects changed.
} else {
    unsigned body_bytes = **attempt;
    // Record population, body_bytes and address under the owner protocol.
}
```

A population of 1..255 declines when the statistic is **at least** the cutoff:
zero declines all such blocks; 225 disables the shortcut. Empty/full blocks
successfully encode as zero bytes. Population is checked before the decision.
Decline leaves destination and effects untouched without admitting them;
acceptance applies ordinary encode checks and exact stores.

The heuristic guarantees no size saving. The owner chooses a cutoff and fallback
against acceptable encoding cost and compression loss; Ikea supplies no default.
The [predictor evidence](../../../workbench/spikes/ikea-composition/probes/ikea-blocks/predictor/README.md)
records the model's accuracy and known limits.

## Coordinate a replacement with its metadata

A replacement can change body length, population, location and dependent summaries.
Readers must observe these coherently. Even replacing an empty block with a full
block changes metadata and summaries: both bodies have zero bytes and issue no store.

The [integration example](../../examples/bec256/integration.cpp) replaces an empty
block with a nonempty body at offset 64. It admits the body and a four-byte
TuplePack directory entry holding population, length and address, then updates a
count contribution and generation under assumed exclusion. The owner can also
replace in place when space and isolation permit it.

The owner resolves issued-store effects and publishes body, metadata and summaries
together. Effects supply neither beforeimages nor rollback. Hooks need admitted
resources and cannot fail or suspend during writes. The
[integration guide](../integration.md) covers leases, cancellation and retaining
state across waits at completed operation boundaries.

## Compose native values

The [native example](../../examples/bec256/native.cpp) decodes two independent
bodies, filters and counts their bits in registers, then encodes two independently
placed outputs. Include `<ikea/bec256/author/write.h>` for these operations.

In the example, the first input contains position 29; the second contains positions
9 and 200. The mask keeps position 29 in the first half and position 200 in the
second. With admitted inputs, two disjoint output grants of 47 bytes each and two
available journal slots, its core is:

```cpp
auto bits = bc::native::intersection(
    bc::native::read_pair(*source_a, *source_b),
    bc::native::load_pair(mask.data()));
const auto pa = bc::native::population(bc::native::part<0>(bits));
const auto pb = bc::native::population(bc::native::part<1>(bits));
auto lengths = bc::native::encode_pair_exact_unchecked(
    bits, pa, output_a, 0, pb, output_b, 0, effects);
```

Both results have population 1 and a one-byte body. `part<0>` and `part<1>` extract
the blocks in argument order, regardless of input adjacency. `population(block)`
counts that block's set bits; `population(pair)` totals both blocks. Intersection,
union and difference operate on decoded bitsets; each output retains its own
population, length and address.

`<ikea/bec256/author/analysis.h>` supplies `native::estimate_bytes` and
`native::enum_bits` for a native block or pair; pair results are two separate
values in half order. `native::encode_if_promising` in `author/write.h` consumes
one native block with the same three outcomes as the ordinary operation.
Several child decisions do not form an atomic group: the owner coordinates their
representations and publication.

The trusted exact pair cannot reject or suspend once the owner has proved its
prerequisites. There is no checked all-or-nothing pair command. If admission
depends on the result sizes, prepare both candidates, budget and admit both, then
execute trusted writes under the owner protocol. Separate checked writes have
separate rejection boundaries. For one native block, `native::encode` checks
population, capacity and effects before an exact write; `prepare_unchecked`
creates a retained candidate with caller-proved population.

A parent that has validated a collection can use
`source::assume_valid(storage, bytes, population)` when resolving each child.
This borrows without rescanning; all `source::admit` framing, population, extent
and initialized-storage requirements must already be proved and remain valid.
Readable bounds are unchanged. Otherwise, use `source::admit`.

The native surface supports AVX-512 and NEON. AVX-512 requires VBMI, BITALG, VL,
BW and DQ; VPOPCNTDQ is optional. x86 builds without those required AVX-512
features, including AVX2-only builds, use the scalar ordinary fallback and have
no native Bec256 author profile.

[author/chain.h](../../include/ikea/bec256/author/chain.h) carries two decoded blocks
through the shared inline/CPS mechanism. The caller defines mask and coordinate
meaning. The [composition check](../../test/bec256/composition.cpp) calls the same
read/filter bodies inline and as stages, including independent inputs and early
completion. A stage does not suspend; the owner may wait after the chain returns.

## Memory access bounds

An admitted source needs only its exact body span. Native reads adapt short spans
with AVX-512 masked loads or NEON scratch. Supplying at least 64 initialized
readable bytes starting at the body permits direct native window reads without
changing body length. The suffix need not be zero, but extending the span grants
permission to read it. Allocation capacity alone is insufficient, and exact
encoding does not initialize the suffix.

The ordinary and checked native encoders write only the emitted body. The trusted
native entries distinguish exact output from a raw 64-byte access grant:

| Entry | Prerequisite and access bound |
| --- | --- |
| `encode_exact_unchecked` | Correct population, capacity for the resulting body and one coverage event; writes exactly that body. 47 bytes suffice universally; tighter proofs are valid |
| `encode_pair_exact_unchecked` | Correct populations, capacity for each body, disjoint output bodies and room for two events; writes two exact bodies |
| Raw `decode_unchecked` / `decode_pair_unchecked` | Valid body/population and 64 initialized readable bytes per input, including empty/full blocks |
| Raw `encode_unchecked` / `encode_pair_unchecked` | Correct populations and exclusive 64-byte writable windows; may write past returned body lengths and record no effects |

All entries in this table are in `bc::native`. Raw encoder input/output storage
must not overlap. Use the exact entries for bodies placed beside other live bytes
unless the owner can separately grant the full raw access windows. The prepared
candidate's `write_unchecked` also writes exactly `bytes()` under its admission
and coverage prerequisites.

## Executable examples and checks

Build with the [pinned Linux toolchain](../../../BUILDING.md). The example targets
are `ikea_example_bec256_ordinary`, `ikea_example_bec256_integration` and
`ikea_example_bec256_native`; the last runs its native path only on a supported
profile. The [wire/replacement check](../../test/bec256/check.cpp) covers wire
correctness and exact page-boundary accesses. The module test targets are
`ikea_bec256_check` and `ikea_bec256_composition_check`.
