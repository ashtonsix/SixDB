# Using Bec

Bec (Bisection–Enumerative Code) encodes bitsets into **headless** bodies. Two
concrete variants share one count-tree core and one lifecycle:

- **Bec256**: one 256-position `ikea::bitset256`, at most 47 bytes.
- **Bec4096**: one 4096-position `ikea::bitset4096` (sixteen 256-position
  blocks), at most 784 bytes, with a block directory decoded once per reader.

Include `<ikea/bec.h>` and link `ikea::bec`. The variants live in
`ikea::bec256` and `ikea::bec4096`; both re-export the shared vocabulary in
`ikea::bec` (`error`, `describe`, `view`). This page states the rules they
share. The [Bec256 guide](bec256.md) and [Bec4096 guide](bec4096.md) cover what
is specific to each; the [representation guide](representation.md) defines the
256-position count tree and byte ranks that both use.

## Choose a variant

| Choose | When | Costs to accept |
| --- | --- | --- |
| Bec256 | Independent small values; fresh, cheap binding; native pairs from two bodies; replacement at 256-position grain | Sixteen populations, lengths and addresses per 4096 positions in the parent |
| Bec4096 | Reusable 4096-position groups: full scans, repeated block reads, ranks and counts with a retained reader | Binding decodes a head; changing any block re-encodes the whole body; terminal-heavy groups encode slightly slower |

Measured on resident data with retained readers, Bec4096 decoded whole groups
in 0.50–0.62× the time of sixteen Bec256 decodes on Zen 5 and 0.82–0.84× on
Neoverse V2, read single blocks at parity, and stored the 14 RealRoaring window
families in 45% fewer bytes **including the parent's metadata** (packed in both
cases). Dense random bodies alone are slightly larger than sixteen Bec256
bodies. A block read without a retained reader pays the head (2–7× a retained
read), and cold-cache and length-changing mutation costs are not measured. See
the [Bec suite](../../../workbench/benchmarks/bec/README.md#bec4096-against-sixteen-bec256-bodies)
and the [graduation study](../../../workbench/spikes/bec4096/README.md).

## Operations

| Operation | Bec256 | Bec4096 |
| --- | --- | --- |
| `encode` (checked, exact), `encoding::make` / `admit_write` / `write` | Yes | Yes |
| `validate`, `const_view::attach`, `bind_reader`, `bind_reader_unchecked` | Yes | Yes; binding also decodes the block directory |
| `decode` to the plain value | Yes; `decode_pair` for two readers | Yes; one fused walk over all blocks |
| One block, its 32 byte counts | — (the body is one block) | `decode(reader, block, …)`, `counts(reader, block, …)` |
| Rank and slice population | Decode, then `bitset256::rank` | `rank`, `population(reader, first, count)` |
| All byte counts | — | `counts(reader, span<uint8_t, 512>)` |
| Size estimate, `enum_bits`, `encode_if_promising` | Yes | — |
| Native values (NEON, AVX-512) | `bec256::native`: blocks, pairs, Boolean bodies, exact writes | `bec4096::native::read`, `read_pair`, `counts`: Bec256's block carrier |

Bec4096's native reads return the same `bec256::native::block` and `pair`
values, so Bec256's intersection, union, population and encoding bodies consume
blocks of either variant. Stream addressing stays behind the Bec4096 reader.

## Interpret a body

A body carries no population, length, address or variant tag. The caller keeps
the population, the exact byte length and the location together and supplies
them to every read; populations 0 and full both have zero-byte bodies. Another
population can describe different valid content: validation checks framing,
not the association, which the owner keeps coherent with the bytes.

`const_view::attach(storage, bytes, population)` checks placement only.
`validate` checks the complete body. `bind_reader` validates once and returns
a reusable `reader` holding its admitted metadata by value (Bec4096 also holds
its decoded block directory); later reads never rescan. `bind_reader_unchecked`
trusts a content proof the caller already holds. Writes or remapping of the
bytes invalidate a reader's proof.

## Read windows

An exact body span suffices for every read. Granting at least 64 initialized
readable bytes beyond the start of each read window lets native reads load
directly: for Bec256 the window starts at the body, for a Bec4096 block at its
stream inside the body, so a granted suffix after the body completes the last
blocks' windows. Without it, short tails are adapted (masked loads or zeroed
scratch). Allocation capacity alone is not a grant. Windows may load bytes of
neighbouring bodies or streams within the grant without decoding them.

## Write and publish

Checked writes test population, destination capacity, effect-journal capacity
and command/journal aliasing before any store, and leave destination and
journal unchanged on rejection. They store exactly the body and record one
issued-span effect against the named `view`; an empty body stores nothing.
Input bits are consumed before the first store, so the input may overlap the
destination; command objects and journal storage must be disjoint.

`encoding::make` prepares a body without owner writes when the owner must wait
for space or coordinate several children. `admit_write` checks a proposed
write and reserves nothing: the owner budgets every child before execution.
Replacing a body changes its population, length and possibly its address;
the owner publishes body, metadata and summaries together. See
[coordinating a replacement](bec256.md#coordinate-a-replacement-with-its-metadata)
and the [integration guide](../integration.md).

## Execution profiles

NEON and AVX-512 (VBMI, BITALG, VL, BW, DQ) builds run the native kernels.
Other builds, including AVX2-only x86, run portable scalar paths: correct on
the supported little-endian profiles, untuned. Value types bridge endian order;
the codecs' internal word reads assume little-endian hosts.
