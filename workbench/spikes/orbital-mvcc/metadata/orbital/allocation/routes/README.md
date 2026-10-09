# Shared Storage routing

Moving the Domain pointer from every Storage to one stable header saves 2,040 B
per 256-identity Group. Packing identity and use state saves another 1,024 B.
Both are usable controls, with a measurable routing cost; neither closes the
complete Object/Allocation metadata budget or establishes a performance winner.

The selected treatments are [`storage8checked-ready.patch`](storage8checked-ready.patch)
and [`storage4-word-loop-ready.patch`](storage4-word-loop-ready.patch), applied
after the captured GrowthRequest + Storage16 tree and
[`Group advice`](../../heat/README.md). Their adjacent cut JSON files identify
the exact changed inputs. Earlier runtime trees remain frozen in ignored
`build/orbital-mvcc-metadata/routes/`; intermediate patch transcripts live there.
Main Orbital is unchanged.

The separate [direct-Domain controls](direct-domain/README.md) put the actual
route before the slots, removing the header pointer load. They preserve these
Ready cuts and compare the same checked API and count limit.

Each Group has a shared `{Domain*, slots[256]}` block. A Window has the same
prefix with one slot and owns its foreign-advice flag. The immutable identity
selects the first slot, then the header supplies the route. Existing Group,
Window and queued-message Arena pins remain authoritative; there is no global
registry, extra lifetime limit or alignment-mask assumption.

The 8 B slot has a 32-bit atomic count and identity byte. The 4 B slot has an
atomic word with identity in its high byte and use state in its low 24 bits.
One return tag leaves 8,388,607 live uses. `hold` and now `retain` refuse
saturation before mutation. The packed CAS loops carry the complete expected
word; an earlier proxy reloaded identity on each attempt and is retained only
as an experimental control. Allocation and Hold remain 32 B; the fallible
`Result<Hold>` is 40 B. Ordinary Shape query packets were not widened.

[`checked-owner.patch`](checked-owner.patch) carries that refusal through
Shape's already-fallible export operation. An unexposed Export is destroyed and
deallocated when its physical pin fails. [`export_failure.cpp`](export_failure.cpp)
checks both export policies and a successful retry with actual SeriesPack bytes.
[`accounting.patch`](accounting.patch) adapts the composed Material's cold
diagnostic to the actual slot array, identity and Window layout. It compiles
against all five control layouts without changing ownership, query packets or
memory charges; missing protected-footprint state remains null, not zero.

## Actual requested costs

At 256 identities and 4 KiB native pages:

| Charged component | 4 B / A | 4 B / B | 8 B / A | 8 B / B |
|---|---:|---:|---:|---:|
| Group, including shared route/header | 1,456 | 1,456 | 2,480 | 2,480 |
| FiniteGroup object | 448 | 448 | 448 | 448 |
| Combined control mapping | 4,096 | 4,096 | 4,096 | 4,096 |
| Live bitmap capacity | 80 | 144 | 80 | 144 |
| Compact trim-span capacity | 1,272 | 2,192 | 1,272 | 2,192 |
| Discard-result bitmap capacity | 40 | 72 | 40 | 72 |
| **Group / populated public Pool total** | **7,392** | **8,408** | **8,416** | **9,432** |

The empty public Pool reports zero, which excludes real setup costs. Add the
first Group-vector pointer capacity (8 B), Arena/Pool/thread/libc setup and all
Object/Shape metadata. These are requested capacities, not allocator usable
sizes or RSS. Window is 328 B in both treatments. The existing control mapping
has occupied bytes 1,968 / 2,488; its slack suggests a separate placement
question, not an implemented saving here. [`census.cpp`](census.cpp) checks the
component sum against actual public charges.

## Directional performance and checks

Two ordered sweeps on the local AArch64 OrbStack guest, Clang 21.1.8, `-O3
-DNDEBUG`, gave:

| Actual operation, ns | Storage16 control | Checked8 | Packed4 word loop |
|---|---:|---:|---:|
| Owner capture + copy + byte read + two releases | 20.0–20.6 | 21.7–22.0 | 22.6–23.0 |
| Two foreign releasers, separate counter lines | 7.6–7.7 | 17.7–18.4 | 26.1–29.8 |
| Two foreign releasers, counters in one 64 B region | 45.8–46.7 | 44.1–46.6 | 44.3–57.0 |

[`timing.cpp`](timing.cpp) pins workers and verifies the counter placement.
Its foreign uses are captured before timing; the Allocation stays live, so
these are non-final returns without advice or queue work. Storage16 retains its
old infallible copy API; compare Checked8 with Packed4 for matched refusal and
owner checks. Timings lack PMU, distributions and realistic workload weighting.
They do not establish database latency or a target-machine conclusion.

Emitted AArch64 code confirms the extra dependent route load. The packed word
loop removes the proxy's per-CAS identity reload, but still decodes identity for
routing. Dense counters can share cache lines; padding every slot would undo
the storage saving. The composed target-worker screen should decide this trade-off.

Eight existing lifecycle/failure tests passed for each checked layout in Release
and fully instrumented ASan/UBSan/LSan; the packed refinement passed the same
sixteen runs. [`check.cpp`](check.cpp) adds NDEBUG-active saturation before mutation,
identity 255, reuse, mixed Group/Window, eight foreign publishers, last-pin queue
custody and held-byte growth histories. Ready copies also compile and run these
checks in Debug. Captured recipes, sources, logs and receipts are under ignored
`build/orbital-mvcc-metadata/routes/`; the [qualification](qualification.json)
identifies the exact cut. The [verified archive](artifact.json) recovers final
and rejected runtime cuts, regression/consumer recipes, emitted code and timing
receipts. Its `final-source.tar.gz` holds the matched source trees; compiler
output remains disposable. Diagnostic patch hashes and compilation limits are
in [accounting-cut.json](accounting-cut.json).

Header recovery is an explicit Clang/Linux experiment. Array subtraction is
valid within the actual slot array; reaching its enclosing prefix uses the
`container_of` idiom outside C++23's reachability guarantees. `std::launder`
does not make previously unreachable header bytes reachable. The primary
[P3407R1 proposal](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2025/p3407r1.html)
explains the language gap and existing compiler practice. Sanitizer passes do
not prove portability, weak-memory correctness or that a compiler promises
this idiom forever.
