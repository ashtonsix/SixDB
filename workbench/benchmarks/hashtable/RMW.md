# Historical RMW and native-batch comparison — 17 October 2026

This report preserves the original RMW experiment and its source receipts.
The [maintained scorecard](ACCUMULATION.md) contains later measurements with the
corrected payload-mixer harness; use that scorecard for the corrected core comparison.
The native-batch column below belongs to this experiment, not that later run.

Maps support existing-only `modify` and upserting `modify_or_insert`, with inlinable
callbacks on the concrete owner, erased descriptor point calls, and a synchronous
once-bound native mutable batch. Guarded forms keep admission over the complete
operation and publish actual value-byte damage. [Usage and obligations](../../../ikea/docs/hashtable/usage.md)
describe exceptions, refusal, aliasing and caller-owned append references.

The existing-only increment is near Abseil on Zen 5, modestly ahead at K30.
New-key increments are about twice Abseil's throughput across these three CPUs.
There are two material losses: Zen 5 K30 50% new (27% more time), and Neoverse V2
K30 existing-only (21% more time). This API removes redundant lookup/replace work;
it does not eliminate all mixed-traffic or packed-value costs.

## Complete increment consumers

Numbers are median ns per update in independent complete loops: concrete Ikea,
actual Abseil, and descriptor native batch. They measure throughput, not dependent
serial latency, and include no MVCC preservation or concurrent writers. Native
ISA is AVX-512 on x86 and NEON on ARM. Lower is better.
`b = 10`, so K14/K30 mean W24/W40 key domains. New-key percentages count distinct
absent inputs after each erase fixture. Eight-byte values are incremented as uint64 values;
complete output payloads and membership are checked against a separate byte-carry
oracle. Three seeds × five repetitions × four rounds: 60 samples per cell.

| CPU | K | New keys | Ikea | Abseil | Native batch |
| --- | ---: | ---: | ---: | ---: | ---: |
| zen5 | 14 | 0% | 3.172 | 3.151 | 3.169 |
| zen5 | 14 | 50% | 17.067 | 21.964 | 17.141 |
| zen5 | 14 | 100% | 12.788 | 26.983 | 12.762 |
| zen5 | 30 | 0% | 2.812 | 3.038 | 2.800 |
| zen5 | 30 | 50% | 16.819 | 13.216 | 16.290 |
| zen5 | 30 | 100% | 12.015 | 26.690 | 11.947 |
| granite-rapids | 14 | 0% | 5.436 | 5.275 | 5.714 |
| granite-rapids | 14 | 50% | 20.513 | 28.096 | 21.015 |
| granite-rapids | 14 | 100% | 16.383 | 33.227 | 16.628 |
| granite-rapids | 30 | 0% | 3.961 | 4.215 | 4.261 |
| granite-rapids | 30 | 50% | 18.968 | 18.543 | 19.693 |
| granite-rapids | 30 | 100% | 14.134 | 30.422 | 14.605 |
| neoverse-v2 | 14 | 0% | 7.902 | 7.904 | 7.830 |
| neoverse-v2 | 14 | 50% | 22.063 | 30.052 | 21.922 |
| neoverse-v2 | 14 | 100% | 16.476 | 31.307 | 16.486 |
| neoverse-v2 | 30 | 0% | 7.076 | 5.867 | 6.923 |
| neoverse-v2 | 30 | 50% | 20.850 | 28.770 | 20.756 |
| neoverse-v2 | 30 | 100% | 15.362 | 30.610 | 15.498 |

Abseil uses mutable `find` for existing-only traffic and `try_emplace` followed
by the increment for new/mixed traffic. Initial-value preparation is included on
both sides. Ikea's native batch timer includes binding once per round. On Granite
that complete batch costs up to about 8% more than the concrete loop; it is not
advertised as zero overhead. Erased per-point descriptor callbacks are supported
but are not the native-batch result.

The frozen `before` column retained in the [full data](evidence/rmw.json) is a
conventional find followed by mixed insertion. It is a redundant two-step
consumer, not the best possible previous RMW implementation. Speedups against
that column should not be used as the product claim.

The [Zen 5 assembly excerpt](evidence/rmw-primary-codegen.txt) shows an existing
primary uint64 increment as `incq 0x40(%rdx,%rsi,8)` in both concrete and native
batch consumers. The temporary logical value disappears on that path. Swiss and
new-key paths still do their required search, capacity and publication work.

## What the result establishes

Combining resolution and value computation removes a redundant lookup/replace
sequence, and complete consumers can inline the existing-primary increment into
one value store. This does not make every traffic mix faster. In this experiment,
Zen K30 mixed traffic takes 16.819 versus Abseil's 13.216 ns/update; ARM K30
existing traffic takes 7.076 versus 5.867. Both losses remain in the table.

Packed search, value addressing, overflow resolution and new/existing control
flow are plausible places to investigate. This experiment does not isolate their
individual contributions or measure the result of a future specialization.
The later [maintained results](ACCUMULATION.md#core-throughput-absolute-examples)
retain the mixed-update and ARM existing-update losses after the harness repair.

## Preservation and verification

The same native binaries retain existing independent/prefetch reads, default
mixed upsert and same-seed growth controls against the frozen promoted candidate.
Read/upsert cells stay within 4% of that control. Growth is source-identical;
the smaller Granite follow-up screen nevertheless has 4.8%/8.3% slower growth
medians at K14/K30 map8. Both remain well ahead of Abseil. The earlier rotating
[full growth qualification](QUALIFICATION.md) remains the broader growth account;
the new outliers are retained rather than silently replacing them.

Historical runner and auditor are [archived](archive.json); **6,660 audited timing rows**
on Zen 5 AVX-512, Granite Rapids AVX-512 and Neoverse V2 NEON, pinned Clang 21.1.8.
Native and ASan/UBSan RMW histories pass on all three. Source receipts and full
rows/disassembly are in the
[Zen 5](evidence/rmw-zen5-artifact.json),
[Granite](evidence/rmw-granite-artifact.json) and
[Neoverse](evidence/rmw-neoverse-artifact.json) bundles.

The final review removed unnecessary storage-retention tokens from the synchronous
mutable binding: a batch may replace Swiss buffers through insertion, so tokens
tied to replaced buffers were invalid. A fallible-storage regression reproduced
an abort before the fix and passes afterward. This change concerns nontrivial
storage; the native benchmark uses heap storage with empty retention. The
[complete Neoverse consumer assembly is identical](evidence/batch-retention-codegen.json)
before/after that removal; [header identities](evidence/followup-source-identity.json)
record the exact one-header delta from the native capture.
Final local [sanitizer checks](evidence/rmw-local-sanitizers.txt) cover that repair,
RMW exceptions/refusals, partial value planes, capacity boundaries and all W=1..64.
The [standalone headers](evidence/rmw-local-headers.txt) also compile independently.
This historical RMW cut preceded mutable accumulation; current RMW contracts are
in [usage](../../../ikea/docs/hashtable/usage.md#update-without-a-second-lookup).
