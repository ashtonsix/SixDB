# Native complete-operation mutation comparison

Fusion removes the per-key mutation-lambda call. Patching only the existing
inline value and tag provides a further large improvement for short bags.
Spill costs remain large, and partial map updates sometimes regress against
fusion alone. This cohort qualifies those separate effects; it does not select
a universal policy or establish broad Abseil parity.

The complete comparisons, including all negative results, are retained for
[Zen5 AVX2](../mutation-values-zen5/),
[Granite AVX-512](../mutation-values-granite/) and
[Neoverse V2 NEON](../mutation-values-neon/). Each host has 59,400 rows from
1,320 fresh processes. Source is immutable: the later nonzero experiment and
any subsequent MM-only implementation combination are **not** in this capture.

## What is compared

`control` is the captured implementation. `fused` forces the owner and
preparation continuations to inline, retaining their whole-record proposal.
`partial` builds on fusion: an existing map value or MM append within its inline
capacity can update the payload directly; MM also updates the count tag. Other
paths retain the fused implementation. Parent damage is published before a
partial write when an observer is supplied. Generated headers live beside each
binary; no production-header change is part of this comparison.

The five layout/baseline arms and histories are identical to the
[value-width panel](../value-widths-native/): K40 maps and multimaps, W2/W8/W16,
384 or 6,144 keys, 8- or 16-byte MM inline budgets and 128- or 192-byte units,
plus ordinary and exact-five-byte-key typed Abseil. Arm0 uses the 8-byte budget
and 128-byte units; arm1 uses 16/128; arm2 uses 16/192; arms3/4 are Abseil.
All bytes, metadata, bag capacities and build-time tier populations match
across variants for every case/phase. Abseil source is unchanged across variants.
The root report checks that equality instead of assuming it.

The schedule shuffles 220 cases with seed947. Each case executes a rotated
variant order and its reverse, using a fresh process for every entry and five
fresh owners/Pools within each process. All nine owner phases are retained,
with complete hit/miss/serial warmup and independent content/update/erase
oracles. Full-value digests drive a shuffled serial cycle through every key.
Timed mutations use the no-observer policy; publication correctness is checked
separately, and this is not publication-performance evidence.

## Results

For W2/A10/MM arm0 with one initial value, the append remains inside a four-value
inline reservation. The same inventory and key distribution hold throughout:
139,568 logical bytes, zero key-overflow rows, and unchanged append inventory.
The absolute append medians make the successive effects visible:

| ns / append | Zen5 | Granite | V2 |
|---|---:|---:|---:|
| Control | 17.666 | 22.214 | 33.591 |
| Fused | 14.812 | 17.961 | 21.312 |
| Partial | 5.927 | 7.433 | 8.054 |
| Ordinary Abseil, partial binary | 3.893 | 4.724 | 6.959 |

Partial is 0.335/0.335/0.240 times control and 0.400/0.414/0.378 times fused.
It still costs 1.522/1.574/1.157 times ordinary Abseil, or
1.385/1.446/1.147 times exact-key Abseil. Its phase-summed lifecycle is
0.664/0.718/0.618 times ordinary Abseil for this history. Initial logical
memory stays at 0.517 times ordinary Abseil; the mutation change does not buy
performance by enlarging the representation.

The change is specific to eligible existing-row edits. The following are
partial / ordinary-Abseil append ratios; lower is better:

| W / initial fanout / arm | Zen5 | Granite | V2 |
|---|---:|---:|---:|
| 2 / 4 / 0: spills on append | 19.238 | 20.883 | 20.196 |
| 2 / 4 / 2: append stays inline | 1.725 | 1.659 | 1.328 |
| 8 / 1 / 1: append stays inline, more key overflow | 3.460 | 3.308 | 2.572 |

The spill row changes only to 0.964/0.989/0.958 times control, and is effectively
unchanged versus fused. Different row capacity still matters: W8 arm1 has
1,261 of 6,144 keys in key overflow. This is distinct from spilling bag values.
The much faster W2 inline cases do not resolve the spill path or capacity trade.

Partial is not uniformly better for maps. At W16/A10/arm0, map update is
1.059/1.110 times fused on Zen5/Granite and 0.902 on V2. Partial still costs
2.675/2.561/1.994 times ordinary Abseil in this cell. All six width-by-geometry
arm0 map cases lose hit, serial and update to ordinary Abseil on every host.
The original three experimental arms remain intact here; choosing different
paths for maps and MM would be a later implementation decision.

Reads are substantially unaffected by this mutation change. Across the 38
arm0 MM width/fanout/geometry cells, median partial/control hit ratios are
1.001/0.999/1.001 and serial ratios 0.999/1.000/1.000. Partial still loses
independent hit to ordinary Abseil in 37/38 Zen5, 36/38 Granite and 34/38 V2
cells. Long-bag append wins remain alongside their other losses: at
W16/A10/fanout32/arm0, partial append is 0.376/0.470/0.268 times Abseil,
but hit is 2.445/1.753/2.414, serial 2.996/1.995/2.251 and lifecycle
5.112/4.097/3.241. No best-arm minimum is used to hide these differences.

The unchanged-source Abseil controls mostly center near 1.0 partial/control,
but individual update cells vary. Their update-ratio ranges are
0.732–1.071 Zen5, 0.840–1.418 Granite and 0.890–1.108 V2; read medians
are within about 0.2% of 1.0. The largest Granite update change is exact-key
W16/MM/A6/fanout1. All process-order medians and ranges are retained. These
controls limit attribution of small differences; they are not used to erase
negative cells or explain every change through code placement.

## Native qualification

Every captured host passes the original observer oracle and partial checks for
value widths 1/2/3/8/16/64. The observer oracle checks all key tiers, raw
before-images, coarse/caller effects, packed-tag neighbors, refusal and retained
promoted material. Timings cover W2/W8/W16 only; correctness checks are not
performance coverage of all supported widths.

All 27 generated ELFs match their compile receipts and actual timing hashes.
Every original spike header recorded by the generator matches the source
manifest. The DWARF audit resolves all 2,160 read/update regions, including
cloned end clocks and outlined timers. All 1,620 read consumers are fused; all
540 effective update CFGs resolve without indirect/unresolved boundaries or
call-graph truncation.

The control's repeated target is specifically
`try_set_bytes(...)::lambda()::operator()()`, not the enclosing method. In every
candidate fused/partial update, the outer call instead encloses the entire
query loop; that per-key preparation boundary is gone. Allocation, key-overflow,
linked-value, guarded field-initialization and cleanup calls remain reachable
on conditional paths. Their presence in a CFG cycle is not evidence they run
on every append. Full demangled selected paths and every effective update
boundary are retained, together with small native instruction excerpts showing
the W2 partial payload and tag stores. No stack-frame size or instruction count
is presented as a timing explanation.

## Measurement limits and recovery

Each host is one machine run. Balanced process order does not eliminate seed,
placement, allocator or temporal variation. Lifecycle sums separately timed
construct/build/update/erase/release phases; queries, oracles, inventories and
Pool setup/drain are excluded. Memory is requested storage plus metadata, with
owned backing reported separately; Abseil backing zero is unknown. Inventories
are snapshots after build/update/erase; population columns always describe
build. There is no transient-peak, RSS, measured cache residence, DRAM throughput,
Loom ring or multiwriter claim. ISA and microarchitecture vary together.

Source manifest:
`163fc648a9b60d8f8120e0f7e430515f7c51ec03d4f6852d7d3b7e268f83baeb`.
Source archive SHA-256:
`4c85d0f8fae3fcffc94b5fe43c48ec70fe791eba72da9a51c71dc01db4fb388c`.
Driver SHA-256:
`310cbb41d43f13fd5d4f862804779666e92a130d12a9bd5327ca13ed538087ad`.
Group: `20261013T214850Z-acc376b1`.

[context.json](context.json) links exact host archives and ELF identities.
Host directories retain byte-identical raw timing rows, schedules, generated
compile receipts, checks and summary CSV, plus separately authored audits and
analysis. Bulky ELF, assembly and source remain recoverable from artifact.json;
the duplicate summary JSON remains in the same archive.

## Audit and report tools

`audit-dwarf.py` binds each ELF to the `bench` entry for its variant and width
in `probe/receipt.json`. It verifies the copied translation unit, generated
`promoted.h`, partial `record_table.h` where present, macro selection, disabled
automatic vectorization, and exact ELF hash. It resolves hit, miss, serial and
update from the copied source's DWARF caller locations. Outlined timers are
opened at their real callsite; phase never comes from lambda numbering or
clock ordinal. Compile/link paths in the receipt may name the native host;
recovered local paths are checked through their byte identities.

`summarize.py` qualifies read fusion separately from the update call graph.
A complete read-loop helper is permitted only outside the caller query cycle,
with its consumer fused and no unresolved hot branches. Updates retain direct
and recursively reachable spike call boundaries, with full demangled names.
An outlined nested mutation lambda is not mislabeled as its outer class method.
Calls inside a CFG cycle are structurally repeatable; that does not establish
that allocations, failure paths or guarded initialization execute on every
query. No dynamic frequency or cost is inferred from this classification.

The update graph is bounded to eight spike-call levels and 512 visited symbols;
any truncation is explicitly recorded and must be reviewed. Runtime and
allocation bodies remain named boundaries rather than being recursively
expanded. Full read CFGs and selected update helper bodies are emitted for
inspection. The original control retains operation boundaries; their presence is evidence,
not an audit failure by itself.

```sh
python3 workbench/spikes/hashtable-design/evidence/mutation-native/audit-dwarf.py \
  RECOVERED/probe/fused/assembly-2.gz RECOVERED/probe/fused/values-2 \
  RECOVERED/probe/fused/value_width_bench.cpp OUTPUT \
  --value-bytes 2 --variant fused --probe-receipt RECOVERED/probe/receipt.json
python3 workbench/spikes/hashtable-design/evidence/mutation-native/summarize.py \
  OUTPUT/codegen.json OUTPUT
```

Repeat for all three variants and W2/W8/W16. Each ELF should expose 80 regions
(two kinds, two geometries, five arms, four audited phases), including 60 read
regions. The unchanged-source typed Abseil arms must remain in comparisons;
changed code placement is not assumed harmless. Generated source, observations
and native timings must all refer to the same captured cohort.

`report.py` is copied byte-for-byte from that frozen source; `qualify.py`
reproduces its archived summary, validates the seed-947 schedule, forward and
reverse variant order, every serialized process and raw row chunk, matched
physical inventory, and the nine generated/timed/audited ELF identities. It
also retains separate process-order medians and ranges, so negative controls
and process variation remain visible. The full panel has 220 cases, six
processes per case, five repetitions per process and nine phases: 59,400 rows
per host. The three retained panels total 178,200 rows. Separate process-order
medians and ranges remain beside the archived complete comparison. All three
compact exports and retained-only report reproduction have been verified.
