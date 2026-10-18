# Hot Part binding control

Local aarch64 Linux, pinned Clang 21.1.8 `-O3 -march=native -fno-lto`, CPU 4.
This compares actual separately compiled `RepresentationSelection::part(index)`
against frozen Git HEAD definitions. It measures hot admitted bindings and byte
consumption, not capture, allocation, cold memory, database latency or end-to-end
query performance. No repository source was changed by the probe.

Each binary constructs its own synthetic admitted 32-Part storage outside timing,
using that version's exact Representation/record definitions. Payloads contain
nonzero uint64 values. A runtime-generated 256-index query sequence selects all
parts; an independent cycle/remainder sum validates every timed result. No capture,
pin, stock disposal or owner transfer crosses versions. The driver sees only
public part declarations; the actual member is instantiated in a separate TU.
One opaque outer function choice per case prevents specialization; there are no
per-iteration compiler barriers. The prebound control consumes the same values.

The final comparison uses 32 million reads per case, five balanced process-order
trials and five case-order rotations. Every case has an untimed warmup. Fresh
processes run sequentially on CPU 4. Raw wall and thread CPU times are retained;
wall samples include visible scheduling outliers during other local work.

Median thread CPU ns/operation, four original layouts:

| Layout | HEAD | New mixed decoding | Switch | Stride table | Cached stride |
|---|---:|---:|---:|---:|---:|
| independent shared | 1.497 | 1.817 | 1.644 | 1.469 | 1.000 |
| grouped shared | 1.502 | 1.802 | 1.632 | 1.450 | 1.000 |
| independent Allocation | 1.502 | 1.824 | 1.641 | 1.463 | 1.018 |
| grouped Allocation | 1.522 | 1.812 | 1.638 | 1.463 | 1.013 |

Prebound medians are 0.374–0.381 ns across versions. Paired median ratios to HEAD
are 1.19–1.21 for new decoding, 1.06–1.10 for switch, 0.97–1.00 for the stride
table and 0.67 for cached stride. `[[unlikely]]` on mixed decoding generated the
same AArch64 instructions and did not recover the regression.

Clang if-converts the original/new ownership branches into unconditional address
calculations and conditional selects. Mixed decoding therefore taxes every old
layout. Cached stride replaces the mode decoding with one byte load and indexed
address calculation. The tested representation header stays 16 B and View 8 B;
the synthetic constructor explicitly supplies the stride. The enum narrows to
uint8 and the private cached field occupies existing header padding. This is the
preferred production candidate; real construction must initialize it and retain
address-first/body-size assertions. Root owns that implementation and correctness
qualification.

`sources.json` records exact old/current/variant header hashes and baseline HEAD.
`context.json` and `confirmation/` retain the initial paired checks. The final
`variants-results/context.json` records source/binary hashes and every compile/run
command; `samples.csv`, per-process CSV/stderr and `summary.json` retain the raw
measurements and comparisons. `*.asm` retains code generation. Source variants
and their patches are local ignored diagnostic material, not maintained APIs.

After production adopted cached stride, `final-public/` froze the actual public
headers. Compiling `binding-v2.cpp` against those headers produced exactly the
same 18 instruction words (72 B) for `part(index)` as the selected variant.
`final-public-codegen.json` and `final-public-binding.asm` retain that check;
no timing was rerun for the final source.
