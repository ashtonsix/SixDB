# Larger-bitset directories with current Ikea

This redoes the original heterogeneous probe's range-count question: resolve a
requested range of headless Bec256 blocks, intersect selected original ordinals
with a separate plain query bitset, and count matches. The body allocation is
identical across directory alternatives. The renewed
[whole-bitset extension](whole-bitsets.md) separately covers independent Boolean
operands, output obligations and storage estimates. Those policies remain in
Workbench rather than becoming part of Bec256.

## Representations under comparison

Population, exact length and location remain caller-owned interpretation.
These are experimental directory schemas, not Bec256 headers or public presets.

| Directory | Per-block description | Exposed metadata bytes at 256 blocks, checkpoint 16 / 64 |
| --- | --- | ---: |
| `direct4` | Population9, length6 and absolute offset17 in a 32-bit word | 1024 / — |
| `direct8` | The same fields in a 64-bit word, with a wider offset | 2048 / — |
| `tuple_absolute6` | TuplePack population9, length6 and four offset bytes | 1536 / — |
| `tuple_checkpoint2` | TuplePack population9 and length6, plus absolute checkpoints | 576 / 528 |
| `series_local` | SeriesPack population9 and Local length6 planes, plus checkpoints | 544 / 496 |
| `series_scan` | Population9 and striped length6 planes, plus checkpoints | 544 / 496 |
| `tuple_folded2` | TuplePack population-code8 and length6, plus checkpoints | 576 / 528 |
| `series_folded_local` | Population-code8 and Local length6, plus checkpoints | 512 / 464 |
| `series_folded_scan` | Population-code8 and striped length6, plus checkpoints | 512 / 464 |

The folded convention stores `min(population,255)`. Code 255 with zero body length
means full (256); a singleton-hole body has population 255 and nonzero length.
Thus length is part of population interpretation. This is valid for these Bec256
bodies; a future mixed representation must establish its own law, rather than
silently reuse the convention with a raw full block of nonzero length.

Checkpoints store absolute offsets every 16 or 64 blocks. Address reconstruction
sums intervening lengths, including lengths of inactive predecessors. Population
frames do not rebase block identities. A caller-defined map from original ordinal
to physical child remains separate from the codec's 256-position coordinate.

Three resolution paths expose the cost of that reconstruction:

- `point` reads individual metadata entries and sums preceding lengths afresh.
- `buffered16` materializes a metadata group through ordinary bound readers.
- `native16` keeps populations and exclusive prefixes in a native frame, reused
  across body pairs. Its entries expose actual length as well as population and
  address. Sequential frames can retain the previous byte frontier within a
  checkpoint; a fresh jump must reconstruct that frontier.

Direct and absolute TuplePack directories use their point path. `direct4` is
omitted when offsets exceed its 17-bit capacity, including the dense 4096-block
input. The 129-block case distinguishes logical tails from physical padding:
Local metadata pads to sixteen entries, while a striped length plane needs whole
128-entry tiles. The counters include those exposed tile extents and checkpoints,
but exclude owner/plan objects and the test allocator's cache-line rounding.
They are not measurements of resident memory or cache traffic.

## Operation and controls

The consumer accepts a nonzero start, requested count, ordinal stride and an
optional mask with one bit per original block. It pairs surviving nonterminal
children even when their addresses are nonadjacent. Population zero skips body
and query reads; population 256 needs only the query population. All alternatives
use this same evidence policy. A fully inactive selection needs no query pointer.

The body owner admits framing and association once before timing and grants a
64-byte initialized read window at each body address. Bodies are densely packed
with one initialized suffix at the end of the allocation. This is a wider read
permission than an exact child span; the codec's exact-read measurements are
separate. The query is plain and follows original ordinal order.

Five execution choices share the logical result:

- Inline decode, intersection and reduction.
- A shared compiled decode/intersect/count region returning a scalar count.
- The same work with an intentionally forced 64-byte materialization seam.
- Two continuation stages (decode, then intersection), followed by count completion.
- One fused decode/intersection continuation stage, followed by count completion.

The shared-body matrix covers every directory, both checkpoint intervals and
three resolution paths where applicable. The other execution choices are measured
for the direct4 control and SeriesPack Local/native16 consumer. There is no
runtime policy switch inside a decoded pair. None of these choices implements
Loom scheduling, an Engine planner or a durable larger-bitset owner.

`metadata_update` rewrites an existing directory entry with a prepared ordinary
writer, including checks and issued-byte effects. It measures neither body
replacement nor length-change propagation. A real mutation must repair dependent
checkpoints/addresses, maintain summaries and publish coherently. Bec256's
[Ikea integration example](../../../ikea/examples/bec256/integration.cpp) shows a
body and independently placed TuplePack metadata entry in one owner protocol;
it is not a packed-directory migration implementation.

## Inputs and limits

Five synthetic shapes use eight inputs each: 256 random blocks, clustered runs,
alternating empty/full blocks, a 129-block random tail, and 4096 random blocks.
The run workload varies the number of full bytes per block. Random bodies are an
entropy/cost probe, not a recommendation to compress incompressible data: Bec256
can expand a 32-byte bitset to 47 bytes. A surrounding representation policy can
choose raw bitsets or another child.

The natural inputs reuse eight retained 65,536-position windows from each of
`census-income`, `weather_sept_85` and `wikileaks-noquotes` in the shared RealRoaring
sample. Query masks are generated independently; they are not observed predicates
or a claimed natural correlation. Scan, stride-17 selection, crossing 15/16,
last block, a 37-block range and an approximately 25% arbitrary mask are measured.

Five repetitions use resident repeated inputs on one pinned CPU. Directory/source
admission and binding are outside timing. Results do not establish cold point
latency, end-to-end database throughput, workload frequencies or a universal
compression/metadata choice. The original eight windows and full source/configuration
remain recoverable through the evidence references.

## What the current directory measurements support

For a regular scan, native frames make packed metadata a modest runtime tradeoff
for smaller extents. The following uses a **fixed** folded population8/striped
length6 choice with checkpoints every 16, rather than selecting the fastest
layout independently for each input. It exposes 512 metadata bytes at 256 blocks,
versus 1024 for the direct4 control. Both use the shared decode/intersect/count
region. Times are microseconds per complete 256-block scan:

| Input | Empty blocks in retained windows | Zen direct4 | Zen packed | V2 direct4 | V2 packed |
| --- | ---: | ---: | ---: | ---: | ---: |
| Random | 0% | 7.366 | 7.571 | 19.934 | 21.657 |
| Runs | Synthetic, varies by window | 6.968 | 7.255 | 18.845 | 20.477 |
| census-income | 35.94% | 4.747 | 4.967 | 12.736 | 14.070 |
| weather_sept_85 | 15.87% | 6.238 | 6.466 | 16.761 | 18.324 |
| wikileaks-noquotes | 86.47% | 1.064 | 1.259 | 2.776 | 3.468 |
| Alternating empty/full | 50% empty, 50% full | 0.109 | 0.303 | 0.199 | 0.839 |

Packed metadata adds about 3–5% on the first four Zen scans and 9–11% on V2.
When population evidence removes most decoding, that overhead matters much more.
The corresponding stride-17 selections cost 18–22% more on Zen and 12–17% on V2
for the first four inputs; wikileaks costs 56%/65% more. The terminal-only
selection is several times the direct control. These are meaningful exceptions,
not evidence that every bitset should use the same packed directory.

Native frames are the useful starting point for packed scans. Repeated point
reads of preceding lengths often lose heavily, and materializing a whole metadata
frame has its own cost. A fresh last-block lookup on random256 takes 85 ns with
direct4 on Zen, 95 ns with the packed checkpoint-16 frame, and 151 ns with
checkpoint 64; V2 is 95 / 109 / 183 ns. **Fresh cursor is not cold memory.** The
checkpoint-64 implementation reconstructs its initial frontier using individual
length reads. These results expose that implementation's cold-entry work; they
do not establish that a bulk-prefix implementation must have the same cost.

The folded population convention also helps updates. Rewriting a two-byte
TuplePack entry takes 5.32 ns on Zen and 10.56 ns on V2, against direct4's
4.31 / 10.18 ns. Folded SeriesPack Local costs 7.22 / 11.92 ns; its striped form
costs 13.82 / 17.72 ns. The journals report 2, 4, 7 and approximately 2.5 issued
bytes respectively. Issued bytes describe preserving stores, not a minimum
logical footprint or a claim about MVCC page-copy traffic. These are same-entry
metadata commands, not complete bitset mutations.

Provisional direction: retain a direct word as a serious latency-oriented choice;
use a retained native frame when a packed directory is justified by space and
access pattern; consider the two-byte TuplePack form when updates matter.
Population folding is useful here, but its dependence on length must remain
explicit. No single schema, checkpoint interval or SeriesPack geometry has earned
universal-default status. Cold access, length-changing owner mutations and
alternative sparse/terminal directory schemas remain open questions.

## The traversal reset

The first selection implementation used a lookahead closure to find the next
nonterminal child. It was compiler-outlined; forcing it inline helped some cases
but left the dense Zen count around 10.4 microseconds. Restoring a direct scalar
load for the decoder's first split field improved decode slightly, but did not
explain that gap either.

The useful change was one forward loop retaining at most one pending child.
There is one metadata/refill body, no per-child iterator call or aggregate
handoff, and no duplicated lookahead path. The shared dense count returned to
7.37 microseconds, with fully inline execution at 6.71. Masks, original ordinals,
terminal evidence and native frame reuse remain intact. This is why directory
ratios alone were insufficient: every directory had inherited the slower loop.
The [outlined](evidence/alternatives/outlined-traversal-zen5.json),
[inlined-lookahead](evidence/alternatives/inlined-lookahead-zen5.json) and
[direct-root-load](evidence/alternatives/direct-root-load.json) captures preserve
the alternatives without keeping their code in the active implementation.

## Continuation granularity and code sharing

A focused repeat prepares both continuation plans before timing and compares
the two-stage pipeline with a fused decode/intersection stage. Each invocation
still processes two Bec256 children. These are microseconds per direct4 scan:

| Input / machine | Inline | Shared region | Materialized | Two-stage CPS | Fused-stage CPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Random, Zen | 6.701 | 7.351 | 6.791 | 9.989 | 10.038 |
| census-income, Zen | 4.328 | 4.743 | 4.394 | 6.433 | 6.459 |
| wikileaks-noquotes, Zen | 0.993 | 1.062 | 1.049 | 1.414 | 1.420 |
| Random, V2 | 21.184 | 19.904 | 21.408 | 20.773 | 20.740 |
| census-income, V2 | 13.618 | 12.750 | 13.772 | 13.314 | 13.331 |
| wikileaks-noquotes, V2 | 2.982 | 2.771 | 3.022 | 2.955 | 2.920 |

Merging the stages and removing per-pair lazy preparation did not resolve the
Zen penalty: the fused form remains 36% slower than the shared region on random
scans. V2 pays about 4% there. The remaining Zen cost is outside the eliminated
inter-stage boundary; these measurements alone do not isolate its instructions.
A further [ordinary-ABI wrapper experiment](evidence/cps-entry-zen5/cases.csv)
contained `preserve_none` clobbers outside the directory loop but did not help:
random CPS was 10.16 microseconds against 7.37 shared. The extra wrapper was
removed. Source `1a675a63cb927f11b5e60b74de05c7d4efea751d9ab28f87b2db4eed14bb7d48`
retains it alongside the successful ARM tail experiment.
This pair-sized CPS arrangement is not the preferred execution choice for this
Zen consumer. The shared compiled region already shares code while avoiding
that cost. The routine pair consumer and the heavier mutation consumer below
separately test whether this penalty persists with different live caller state.

Across the same 27 directory/resolution driver instantiations, text sizes are:

| Machine | Inline | Shared region | Materialized | Two-stage CPS | Fused-stage CPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen | 107,213 B | 67,727 B | 107,892 B | 71,364 B | 71,364 B |
| V2 | 127,440 B | 63,764 B | 127,988 B | 64,164 B | 64,164 B |

These symbol totals exclude common codec/stage bodies, constants, debug data
and other code. They demonstrate reduced driver duplication, not a whole-library
binary-size or compilation-time win. No isolated build-time comparison between
the execution choices was made. This directory consumer therefore supports the shared region as its useful
default. It does not establish a universal penalty for native continuations: the
[routine pair consumer](../../benchmarks/bec256/README.md) has a much smaller gap.

The [Zen supplement](evidence/cps-zen5/cases.csv) and
[V2 supplement](evidence/cps-neoverse-v2/cases.csv) retain all five repetitions
for both direct4 and SeriesPack Local/native16 under scan, stride-17 and arbitrary
selection. Their adjacent artifact references retain symbols and checks. Source
capture: `22bbb947c76f559a1295b5bc5924e7245e4642b980328dc9c3a0b005a8544ab6`.

## Read–filter–re-encode as a mutation consumer

A separate pair operation reads two independently addressed exact bodies,
intersects a synthetic 512-bit mask, computes both output populations, and
writes two exact bodies at independent offsets with issued-byte effects. Its
preflight checks both outputs against the independent wire encoder and verifies
neighbour preservation. Source admission is outside timing. The ordinary path
materializes 64 plain bytes and uses checked writes; the native checked path
shares native values with the encoders. The admitted path additionally assumes
both output grants and the combined effect budget have already been proved.

| Input / machine | Ordinary checked | Native checked | Native admitted inline | Shared admitted region | CPS admitted |
| --- | ---: | ---: | ---: | ---: | ---: |
| Random, Zen | 102.84 ns | 96.47 ns | 94.81 ns | 95.53 ns | 95.38 ns |
| Runs, Zen | 102.37 ns | 95.88 ns | 94.04 ns | 94.69 ns | 94.80 ns |
| Empty/full, Zen | 90.15 ns | 81.32 ns | 81.40 ns | 82.20 ns | 85.69 ns |
| Random, V2 | 245.35 ns | 255.87 ns | 244.15 ns | 223.53 ns | 223.23 ns |
| Runs, V2 | 241.65 ns | 252.34 ns | 241.81 ns | 222.93 ns | 222.43 ns |
| Empty/full, V2 | 209.82 ns | 210.78 ns | 205.33 ns | 194.03 ns | 193.98 ns |

The three admitted choices call the same explicitly inlined read/filter and
population/encode bodies. The shared region encloses the whole operation in an
ordinary compiled call; CPS hands the filtered 512-bit value to its encoding
completion in native registers. Plans and owner grants are prepared before timing.
None of these admitted choices performs ordinary command rejection checks.

CPS is within about 1% of inline execution on the random/run Zen inputs and
about 5% slower on terminal inputs. On V2, a shared compiled region is faster
than the larger fully inlined caller, and CPS preserves that benefit. This is a
useful Bec256 operating point with real exact output/effects, not only a tiny
stage demonstration. It does not erase the separate directory/CPS penalty.

Checked native calls help Zen; on V2 they can still cost about 4% more than
ordinary materialization. Native value handoff alone therefore does not promise
an operation-level win. The owner should select an enclosing compiled region
when that is the useful sharing boundary. Body/directory relocation, summary
writes and publication remain outside this comparison.

The [Zen](evidence/whole-zen5/cases.csv) and
[V2](evidence/whole-neoverse-v2/cases.csv) repeat uses source
`cc76b4ba960b2c801ef4c0d0a41abae442753ced0b3e9aae48c3d6195ea8bff4`.
Output/journal addresses escape before timing. Explicit inlining of the authored
bodies avoids mistaking compiler-outlined calls for the inline control; the
[earlier experiment](evidence/alternatives/whole-before-explicit-inline-zen5.json)
is recoverable but is not this comparison's baseline. The ARM tail implementation
is the one in the [codec findings](codec-findings.md).

## Retained evidence

[Zen cases](evidence/composition-zen5/cases.csv) and
[V2 cases](evidence/composition-neoverse-v2/cases.csv) retain five repetitions,
medians and explicitly named counters. Both use source capture
`001196448afc84d99b1dbd830a196a8eee72b67cac9300064997eea0ae6cd6a4`.
Their artifact references recover source, compiler settings, checks, binary,
symbol sizes and raw samples. The [codec findings](codec-findings.md) retain the
broader population screen and distinguish the contracts of its controls.

To repeat the directory and pair-mutation comparison from the Linux checkout:

```sh
python3 workbench/tools/worker.py run workbench/spikes/bec256-composition/run.sh \
  --machine zen5 --instance-type c8a.medium --arg avx512 \
  --env 'BEC_FILTER=^count/.*/shared/|^metadata_update/|^pair_mutation/' \
  --env BEC_MIN_TIME=0.005 --env BEC_REPETITIONS=5
```

Use `--machine neoverse-v2 --instance-type c8g.medium --arg neon` for V2.
The [prepared windows reference](../bec-packed-metadata/windows.json) identifies
the reused input; the runner fetches it through the shared dataset helper.
