# Owner-side qualification and preparation result

This account closes the owner investigations for consolidation. It establishes
a bounded immutable UInt64/H0 binding and private-afterimage composition,
with explicit unsupported surfaces. It does not promote a public Frame API.
The [usage and contract](README.md) and [comparison scorecard](../controls/SCORECARD.md)
keep semantics and performance separate.

**Landing scope, 25 October:** checked private preparation is excluded at Ashton's
request. Its evidence below is retained research, not a qualified initial write
path. Immutable binding and construction/emission remain in scope; no preparer,
mutation scratch or private mutation driver is extracted into the initial core.

## Current conformance and source epochs

| Epoch | Qualified surface and result |
| --- | --- |
| `ef08bed8`, local Linux AArch64 | All seven owner checks pass normally and with owner/runtime/Orbital ASan/UBSan: 559 description, 284 manifest; material lifetime checks; 1,953 valid images, 991 guarded truncated prefixes, 26 malformed cases; 785 basic history, 7,495 stream binding and 350,630 stream-history checks. This includes both interval forms and the described/refused RLE/head placeholders. |
| `d4ad21a4`, earlier local baseline | Checked sizes/relative-ends G16/G64 admission and same-version conversions, checkpoint-slot negative witness and signed-evidence boundaries. It predates the placeholder extension; its earlier counts are not the current source qualification. |
| Native shared captures | Jobs `20261024T223705Z-dec974e4` / `20261024T223751Z-8ddba6a7` captured owner `e90542d9`: sizes only, 142 manifest / 2,700 binding / 87,718 stream-history checks. Shared stage `a74e1c44` supported both forms, but that does not qualify the newer checked owner. |

The current local overlay uses shared stage `a74e1c44` and matching SeriesPack
`0d44d527` headers/archive. `build/frame-owner-relative-v1/qualification-placeholders.json`
seals its 60 source identities and results. `placeholders-finish.log` and
`placeholders-san.log` retain the normal/SAN runs. All production/check TUs
were rebuilt against the renamed node member; the normal build completed two
corrected test TUs after compiling the production set, while SAN rebuilt all.

Owner, runtime-width bridge and Orbital code are instrumented. The linked
SeriesPack archive is **uninstrumented**; hardware guards cover image boundaries.
These checks establish local NEON correctness, not native CPU performance.
The overlay reuses the earlier matching runtime capture without copying or
rebuilding codec providers. Later source/dependency changes inherit no receipt.

## Consequential witnesses to preserve

- Canonical byte fixtures distinguish stable H0 bytes from newly described
  run/head kinds that old strict readers reject. Block-local runs use a common,
  data-dependent run-ordinal domain, not parent rows or a fixed checkpoint ratio.
  Existing shared runs and real SeriesPack physical head planes supply the
  reference witnesses; checked factories refuse their image execution.
- A structurally valid checkpoint child field changed from 2^63 to 2^63+1
  leaves every decoded logical value unchanged but changes relative SUM by one.
  Attachment refuses it. Removing only the zero-checkpoint-field proof makes
  the independent negative control fail. Leaf validity and ordinary decode
  equivalence do not establish every aggregate law.
- Exactness covers -2^63 (accepted), +2^63 (false exact flag required), adjacent
  differences that fit while a relative offset does not, and large jumps
  between checkpoints whose relative offsets remain exact. Conservative false
  evidence is legal; false last values and unsound true evidence refuse.
- Complete stream/Part metadata and availability are checked before resolving
  addresses. Sparse/missing dependency guards, K0/empty exception regions and
  partial tails exercise the complete request admission boundary. FILTER/SELECT
  preserve output sentinels on refusal and original-row order on success.
- Domain-only COUNT/FILTER and empty fine demand settle without payload.
  Selection remains separate from read authority. A root Part alone does not
  automatically retain checkpoint payload in another Part.
- Real Orbital histories keep old interpretations/readers after teardown,
  publish root/checkpoint replacements together, distinguish logical updates
  from same-version conversion and retry explicitly against a named fresh
  basis. Valid metadata-only remapping defeats shallow address/pipeline checks.

## Completed O3 preparation experiment

[Retained samples and source identities](evidence/prepare-local-o3.json) cover
65,536 original rows, touched group 7 (4,096 rows), six rotated rounds and eight
independent preparations per timed round. The frozen owner is `ef08bed8`,
shared stages `a74e1c44`, matching compiled runtime leaf/SeriesPack `0d44d527`;
Clang 21.1.8 O3, armv8-a+simd/neoverse-v2, local Linux AArch64 VM, CPU 8.
No compiler was active and the selected CPU passed the idle admission check.
Source/binary identities were sealed and checked after execution.

Microseconds per independently prepared batch, group-only / attach-all:

| Column | 1 command | 8 commands | 64 commands | Copied / emitted image bytes |
| --- | ---: | ---: | ---: | ---: |
| OS | 21.282 / 30.318 | 45.740 / 54.928 | 57.069 / 67.017 | 4,691 / 4,691 |
| UserID | 26.493 / 35.475 | 54.486 / 64.876 | 70.832 / 82.957 | 25,620 / 25,620 |

Group-only preparation removes about 9–12 µs of eager attachment work per batch
in this cohort. Batches 8/64 exhaust slack and rebuild once; one-command batches
do not rebuild. Both arms charge touched input/output image admission, private
copy/binding, journal allocation, ordered commands, exact final SUM/min/max and
contribution, and final emission. Every output, original-row answer, effect
coverage and unchanged source is checked outside its timer.

The eight calls are **independent drafts from the same original**, not successive
publications. Construction, returned-image disposal, publication and durability
are outside. This is not a native-owner comparison or complete write-history
speedup. A local O3 observation does not fill native Zen/Graviton cells.

Both paths request 4,096 journal entries per preparation, 128 KiB on this ABI,
independently of their per-command live set. Source/output proofs and final
statistics are also real charged work. Scratch reuse/right-sizing and reuse of
an immutable checked source proof are opportunities, not measured explanations
or changes made in this experiment. A future native comparison must use genuine
typed storage, adapt once per batch when needed and avoid an artificial second
emission copy. These investigations are deferred during consolidation.

Full local commands/logs and the qualification receipt remain at
`build/frame-owner-relative-v1/prepare-qualification.json`; the compact evidence
above preserves every raw timing record. Later stage work, including MAX and
point-plan changes, is outside this frozen preparation source epoch.

## Payload accounting and implementation limits

The basic functional history records 3,680 successful-private-source-copy bytes
and 7,602 emitted bytes across preparation attempts, including the candidate
later rejected as stale. It publishes 12,638 fresh image bytes and reuses 4,073.
After conversion, the original reader alone retains 2,134 extra payload-capacity
bytes. These fixture totals are not per-publication costs, traffic or RSS.
Refused preparation work, scratch allocations, material metadata and allocator/
control-block overhead are excluded. PMR resource allocations and interpretation
vector capacities are accounted separately from payload backing.

Append/conversion and interval changes in these histories use full rebuilds.
`prepare_group` supports basic single-stream afterimages; it supplies no checked
interval point-setter, general in-place lock protocol, concurrent transaction,
durable/recovery proof or retained asynchronous preparation API. Exact extrema
in a prepared result do not imply a public checked MIN/MAX query binding.

Earlier local capture sources/commands live in `build/frame-owner-runtime-v1/`,
`frame-owner-stream-v1/` and `frame-owner-core-v1/`; they are historical provenance,
not substitute qualification for a later source. The checkpoint-negative receipt
and old-reader/basis counterexamples remain retained with the current overlay.
