# Presets and recoverable representation

The public starting collection is deliberately small. Presets are construction
policy, and may change after measurement; they are not persisted format IDs or
claims that one layout always wins. The parameter H is zero, eight or sixteen
bits, no greater than K, and defaults to zero for every preset. Head separation
is independent of the remaining-payload policy: `preset_format<20, preset::bulk_x86, 8>`
combines an eight-bit head with the x86 bulk choice for its 12-bit payload.

| Preset | Striped remaining widths K−H | Intended starting point |
| --- | --- | --- |
| `compact` | None | Uniform Local wire family; point access, small objects and simple composition |
| `bulk_x86` | 1..7, 12, 20 | Curated width-dependent scan recipe |
| `bulk_arm` | 1..7, 10, 12, 14, 15, 20 | ARM-oriented selection including additional mixed stripes |

All other remaining widths use Local. ARM-oriented bytes can be read on x86;
ISA dispatch and execution grain are independent of these choices.
`describe_preset<F>(count, tile_spacing::tight)` suggests minimal independent
plane strides; `cacheline` rounds each present plane's tile stride to 64 bytes.
Owners can instead supply strides and offsets for interleaving. No preset chooses
a buffer pool, partition size, record schema or segment migration policy.

The implementation retains 76 headless formats / 206 placements for explicit selection
and validation, but does not expose 206 named recipes. Borderline choices:

- Striped10/14/15 remain explicit and in ARM-oriented recipes. They do not earn
  an unconditional x86 default merely by winning isolated primitive cases.
- Cacheline spacing remains an explicit placement choice. Its locality benefit
  depends on siblings and access patterns; padding every object by default costs
  space and can hurt scans.
- One-bit extraction, complete-stripe writing, exact-width native stores and
  whole-body AVX-512 permutation earned inclusion through family-level gains.
- A separate named ScanPack/LocalPack facade, per-width threshold dispatches,
  additional tail dialects and automatic layout tuning are excluded from this
  initial surface. Explicit formats and the same native functions remain usable.
- AVX2 bit-mask packing earned a place for one/two-bit residuals. Four-bit
  packing did not benefit from the same treatment; a full native transpose
  remains the simpler basis. Measurements decide whether larger grouping helps.
- CPS is an execution choice, never a different container or physical format.

## Byte-layout tour

For Local payload width 11, each eight-row tile holds eight high payload bytes
followed by three residual bitplane bytes. Values 8..15 have high byte 1; their
low three bits are 0..7:

```text
logical values:  8  9 10 11 12 13 14 15
payload bytes:  01 01 01 01 01 01 01 01 | aa cc f0
                 row-ordered high bits  | low-bit planes 0, 1, 2
```

Within each residual byte, bit j belongs to original tile row j. The body is
little-endian per row; its residual bits are the low bits of the logical value.
Head separation removes the highest one or two bytes into their own row-ordered
planes, independent of this remaining-payload law.

The [ordinary example](../examples/seriespack/ordinary.cpp) interleaves a Striped12 payload
and eight-bit head for a 20-bit value in a single partition:

| Within each 64-row tile's 192-byte placement stride | Occupancy |
| --- | --- |
| 0..95 | 12-bit striped payload |
| 96..159 | Highest byte for each original row |
| 160..191 | Gap, preserved for an owner/sibling |

Both planes have stride 192; their origins differ by 96 bytes. This is a placement
choice, not a different descriptor family or an execution policy.

## Descriptor v1

[representation.h](../include/ikea/seriespack/representation.h) supplies a 40-byte,
little-endian descriptor and checked parsing. It records the resolved physical
choice, not a preset name. Unknown versions, tags and reserved fields fail closed.

| Bytes | Meaning |
| --- | --- |
| 0..1 | ASCII `SP` |
| 2 | Descriptor/wire-law version, currently 1 |
| 3 | Physical family: 0 Local, 1 striped |
| 4, 5 | K and H in bits |
| 6..7 | Reserved, zero |
| 8..15 | Logical count, unsigned little-endian u64 |
| 16..39 | Payload, head0, head1 tile strides, three little-endian u64 values; absent planes have stride zero |

Engine retains each plane's partition/object identity and offset alongside the
descriptor, and retains expression-node identities, transform semantics and
edges for compound containers. Neither raw pointers nor the in-process recorder's
source addresses are a persistence format. Different segments in one collection
may retain different descriptors indefinitely. Recovery resolves their actual
descriptions and owner mappings; it must never rerun a current preset to guess
old bytes.

`attach_representation<F>` checks an exact physical match before admitting the
supplied plane spans. The descriptor identifies representation; actual residency,
accessible extent, base alignment and overlap still require view admission.
A runtime owner can choose a compiled typed binding or use the compiled dense
headless binder. This does not instantiate an erased mutation table for every
possible composition.

Version 1's byte law is executable in [wire.h](../include/ikea/seriespack/detail/wire.h)
and [point.h](../include/ikea/seriespack/detail/point.h), with independent prior-wire
tests. Local holds eight positions per tile: high payload bytes in row order,
then R bitplanes, each byte holding that residual bit for eight rows. Striped
uses 32-byte lanes and its versioned `tail_bit`, `body_offset` and `stripe_offset`
maps; its physical tile is 32×(8/gcd(R,8)) rows. Head planes hold successive
leading bytes in original row order. Native byte bodies are little-endian.
Construction zeroes final-tile slack in each owned field. Any future change to
these laws needs a new physical version, regardless of unchanged preset names.

For exact striped recovery, let R be payload width modulo eight, g = floor(row/32)
and lane = row modulo 32 within a physical tile. Residual bit b is stored at
`stripe_offset(floor(tail_bit<R>(g,b)/8)) + lane`, at bit
`tail_bit<R>(g,b) modulo 8`. Body bytes use `body_offset(row)` and little-endian
row values shifted right by R. The executable mapping includes the intentionally
nonlinear R=3 and R=6 cases; the [frozen independent fixture](../test/seriespack/reference/README.md)
retains the same law with different implementation. Those mappings, head order,
body endianness and slack policy belong to v1, not to a mutable preset heuristic.
