# Choosing the next composition probe

2026-09-10 discussion after [consolidating the integer spike](README.md).
The user selected the [heterogeneous exercise](../ikea-heterogeneous/README.md)
after this comparison. The alternatives below record that decision context.
Performance and composition remain joint requirements, including the [unresolved narrow costs](measurements.md#continuous-wire-bulk-and-register-masks)
and [width-56 Zen encoder tradeoff](wide56/README.md#hardware-findings-2026-09-10).

| Direction | Question it answers best | What it leaves open |
| --- | --- | --- |
| **Heterogeneous bitsets + integer metadata** | Can independent child layouts, metadata-dependent addresses and native work grains compose without leaking into every kernel? | Broad integer coverage and exception reconstruction |
| **PFoR-like reconstruction** | Can a common path combine with sparse exceptions, auxiliary inputs and selective patch work? | Variable-length bitset framing and metadata/body lifetime obligations |
| **Competitive kernels across k=1..64** | Does the locality/body approach extend across widths with strong performance on each target? | Heterogeneous authoring, dependent addressing and multi-source handoffs |

## Recommendation: one heterogeneous read

The strongest next composition question is: **can the metadata representation
or placement change while preserving the association between logical block,
population and encoded body, without changing the BEC body or its consumer?**
The design task independently favours this direction. It challenges more of the
open [native-grain and obligation questions](../../operation-granularity.md)
than another width or a more elaborate body/tail join.

A collection of 256 Bec256 bodies and attached integer metadata is a useful
example, not a required primitive extent. Hold one read operation and consumer
fixed while comparing two actual metadata realisations. The metadata must
supply the correct populations and enough information to locate variable-length
bodies. Lengths, offsets, checkpoints and derived addresses are alternatives
to examine, not a schema already chosen here.

The current integer surface is not automatically enough: cardinalities include
256, while a BEC byte length fits six bits. Wider metadata fields require a
small body/tail composition or a declared plain control; they should not be
silently narrowed to fit the current 1–7-bit endpoints. Sparse/dense metadata
values and the zero-byte empty/full bodies should remain representable.

The concrete seams worth exposing in that read are:

- A metadata reader yields several entries while an AVX-512 BEC region wants
  two independent bodies and populations. Who retains unused entries, and how
  do the two streams keep their logical coordinates?
- The [trusted BEC contract](../ikea-blocks/README.md#bec256-byte-contract)
  requires 64 readable bytes per decode input, although a body is at most
  47 logical bytes. Who admits those reads, including the final body? Dense
  framing, accessible extent and logical encoded length are different facts.
  Padding every child, an exact-access implementation or a scratch bridge would
  be explicit alternatives with measured costs.
- Does a curated metadata/address/decode region outperform a split at resolved
  body addresses? The comparison should expose both useful code sharing and
  transport/retention costs, using the same observable operation.

The enclosing operation needs its own metadata-plus-payload access accounting;
child locality alone cannot establish it. The useful authoring evidence is the
actual substitution edit and where its obligations are discharged. This does
not require selecting a universal fragment protocol, mutation interface or
compressed decode–operate–encode algebra.

PFoR is attractive if exception dependencies are the uncertainty to pursue.
Full kernel coverage is attractive if the immediate priority becomes breadth
and performance closure. Neither is a prerequisite for the heterogeneous read,
and choosing that read would not settle the outstanding kernel work.
