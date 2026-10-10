# Fused Inline reads

Coherent scalar fusion is selected in the live composed spike. The
[288-trial feedback](../../../../performance/FEEDBACK.md#coherent-read-fusion)
compares it with the narrow point writer fixed in both cuts, using the same
canonical Ikea consumer for mapped LMDB. Tiny/larger Inline scalar point reads
move 26.38 → 21.46 ns and 98.26 → 88.14 ns; larger dependent reads still trail
mapped LMDB slightly. Batch Inline is not fused. Current-borrow remains a
separate, untimed lifetime mode. These quick, time-blocked comparisons are
directional, not a broad below-LMDB result.

Live integration preserves the generic Physical seam: friend access now resides
in `candidates/store.h`, and the focused check uses caller-owned
`Prepared(store)` with status returned from `prepare(scope, changes, output)`.
The authored patch below remains the original experimental treatment, not an
apply instruction for the current tree.

## Original isolated control

`read-fusion.patch` is an authored treatment against
`0acd622de2e0219f2029843ec6b553ffc640e435`, qualified in an isolated source tree.
SHA-256: `bfecbfc7b2aee1c6f408ad482ab00a04f04a6710bb8e404e3fe32172022a1bca`.
It passes `git apply --check` against both that qualified base and integrated
baseline `3fabe9e34f4f81886e62fee1a025e950b473e3e9`; the latter composition has not
been executed by that qualification run. Its live baseline was unchanged.

The executed patch had SHA-256
`4639e4efbc202854cb857821532a93483317da4c7cdd1d063727f88e95d96770`.
This revision changes only the CMake hunk: stable context around the composed
library and registration in `metadata_checks`, which supplies assertions,
the metadata test label, timeout and validation-target dependency. All nine
behavioral postimages are byte-identical on the qualified base; the frozen
executed source, binaries and codegen are unchanged. `qualification.json`
records the distinction. That original qualification did not rebuild the revised
CMake wiring or run a benchmark or sanitizer campaign. Subsequent measured
feedback and live integration are separate evidence.

One ordinary TU combines opaque Object lookup, actual Inline history selection
and complete native Tuple consumption. Its explicitly friended dependencies are
the authoritative Store/Scope, TipDirectory/TipObjects, Backend and Material.
It keeps the found directory cell instead of returning a handle and opening it
again. Full Object identity, hash collisions, tombstones and the enrolled cut
still govern selection. Retained ancestors use the existing nodes and registry.
No directory layout, physical layout or payload kernel is copied.

One exact Shape identity check consumes Material's cold `TupleProof`. That proof
already establishes the canonical transfer operands, allowing specialization of
the existing Ikea kernel without loading runtime transfer controls. Actual
captured count/backing are used. Stored Shape/ReadLaw bytes remain allocated and
charged. The mapped-LMDB `read_mapped_proven` entry receives the same canonical
specialization, retaining exact count/extent/null/overflow admission and its
transaction-owned format association and backing lifetime. Include this part of
the patch in a paired comparison; codec improvement is not catalog improvement.

`InlineReadFusion::consume(owner, scope, object_id, operation, output)` returns
small status into caller-owned output. The existing scalar performance trial
calls it through `Bodies<1>::consume_into`; acquisition and destruction stay
inside the complete trial. Batch, escaped Body and Selection behavior remain
unchanged. Converted Series primary and scan use the established fallback.

`consume_current(owner, object_id, operation, output)` is a separate entry which
obtains the existing `Store::Borrowed` inside the complete call. It performs no
Scope enrollment or release. This embedded read cannot yield, invoke caller
code or publish, and no Body escapes. The owner remains live throughout. This
entry is qualified and has a complete caller in `caller_probe.cpp`, but is
**not silently substituted into the Scope-based performance trial**. A subsequent
current-Object measurement must identify this lifetime mode separately.

The new focused native check passed 2,522 independent comparisons and 12
refusals. It includes first old-cut discovery after three real publications,
conversion followed by a converted primary, colliding full Object IDs,
tombstones, a newly inserted Object with no Corpus ordinal, same-cut replacement
while an earlier alternative Body stays live, cross-owner refusal and intact
output on refusal. Current reads preserve the existing Scope count; reads add
no metadata allocations. The existing composed and Shape read checks also pass:
three focused CTest targets, including 32,936 mapped calls and 126 read refusals.

Pinned Clang 21.1.8, x86-64-v3/Zen5 `-O3`, assertions, separate-TU codegen gives:

| Complete caller | Text bytes | Own frame bytes |
| --- | ---: | ---: |
| Baseline one-operation Scope probe | 293 | 152 |
| Fused one-operation Scope probe | 340 | 120 |
| Explicit current-borrow probe | 163 | 40 |
| Captured baseline throughput trial | 2,537 | 360 |
| Fused throughput trial | 2,771 | 344 |

The fused Tuple branch has no intermediate resolve, Material, Shape or native
operation calls. Its Scope/current entry bodies are 922/930 bytes, each with a
72-byte frame; non-Tuple fallback and error calls remain. The matched mapped
entry is 480 bytes with a 40-byte frame and no native-operation call. Whole trial
text grows and still includes cold alternative/fallback paths. Frames include
saved registers but exclude return addresses and are not whole-path stack peaks.
In this original qualification the measured Zen5 executable was the baseline;
control objects were cross-compiled codegen, not measured performance. Use the
linked later feedback for measured caller code and timings.

Private source/build, commands, recovered baseline disassembly, `codegen.json`,
`checks.log` and `qualification.json` are under
`build/orbital-mvcc/composed/read-fusion/`. No additional retained metadata or
catalog-sized cache is introduced.

Configure the current tree with `-DSIXDB_SPIKES=orbital-mvcc` and use these
focused targets (prefix Linux commands with `orb -m ubuntu` from
macOS). The check also belongs to the normal `orbital_mvcc_metadata_validate`
target:

```sh
cmake --build BUILD --target orbital_mvcc_composed_read_fusion_check \
  orbital_mvcc_composed_check orbital_mvcc_consumer_shape_read_check -j2
ctest --test-dir BUILD \
  -R '^orbital_mvcc_(composed(_read_fusion)?|consumer_shape_read)_check$' --output-on-failure
```

The scalar Inline trial and matched mapped-LMDB bridge now use this treatment.
Further comparisons must keep the same command, host, CPU and cases as their
untreated pair. The current-borrow entry needs a separately identified caller;
do not report it as a retained/coherent Scope measurement.
