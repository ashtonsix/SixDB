# Checked point access without constructing a bound reader

The isolated checked-get candidate improves all six measured checked calls on
Zen, GNR and V2. It keeps the checked index rejection and selects the existing
arithmetic point function directly. It avoids constructing a complete bound
reader for one point. No public interface, physical layout or point algorithm
changes, and canonical callback selection is a separate candidate.

The candidate remains in Workbench. In particular, an unchanged V2 bound
Striped5 control regresses materially. The completed layout discriminator
explains much of that linked-program effect, while retaining a measurable
residual; semantic equivalence alone is insufficient to promise equal cost.

## Matched hardware evidence

The [predecessor baseline](../seriespack-predecessor/findings/baseline-20260911.md) supplies the original
strongest-profile programs. Each host compiles only operations.cpp, replaces
that member of the three-object production archive, and links the same
benchmark and checker objects. The independently audited original benchmark
is byte-identical to the baseline. Operations, all available public-wire and
static-point checks pass. The existing independent wire fixture also verifies
all six gapped layouts, all 4096 input indices, invalid end index rejection and
unchanged source bytes.

The order is base/candidate/candidate/base, three sequential repetitions with a 30 ms minimum
per block on CPU 0. Each case has six samples per variant; the table reports
median CPU ns/query. Every checked improvement has separated sample ranges
and agrees at both ordering edges. These are whole-call measurements: subtracting
the bound time would not isolate a binder cost. The
[complete paired table](../seriespack-predecessor/evidence/delivery-baseline-20260911/checked-point.csv)
retains all 432 timings and every bound control.

| Actual gapped layout | Zen base → candidate | GNR base → candidate | V2 base → candidate |
| --- | ---: | ---: | ---: |
| Local1 |25.308 →10.148|20.184 →10.879|11.033 →5.013|
| Local6 |26.339 →10.884|21.361 →12.126|11.198 →5.344|
| Local23/H16 |29.553 →12.058|25.783 →14.181|11.755 →6.166|
| Local56 |26.366 →10.939|21.729 →12.636|11.304 →5.048|
| Striped5 |26.118 →10.486|20.797 →11.909|11.995 →7.392|
| Striped28/H16 |28.786 →10.621|24.235 →12.397|11.961 →6.034|

All six bound controls overlap on Zen and GNR. V2 has five near-parity controls,
but bound Striped5 rises 2.392 →3.214 ns,34.35%, with separated ranges and
agreement at both edges. The actual 116-byte point endpoint and 27-instruction
timed bound loop are identical. The endpoint moves 64 bytes; the timed loop
retains its original address. No changed bound algorithm explains this result.

## Completed V2 layout discriminator

The [retained reconstruction and measurements](../../executable-placement/evidence/checked-point-layout-20260911/summary.md)
restore the original callers and libraries, reproducing both previous programs
byte for byte. Padding the candidate checked-get section with 48 bytes of
unreachable AArch64 NOPs restores all other text addresses/bytes and the original
rodata address/content. Executed checked-get instructions remain the candidate's;
this is a diagnostic link arrangement, not a production padding proposal.

The six-block base/candidate/padded/padded/candidate/base pair gives bound
Striped5 medians 2.380/3.283/2.532 ns. Restoring placement removes most of the
38% loss in this run, but the padded program remains 6.4% slower at both edges.
Local1 bound remains 2.176 ns throughout. Checked Local1 retains its improvement
at 10.474/5.020/5.027 ns, and checked Striped5 at 11.233/7.347/7.262 ns.
No particular predictor, cache or allocator mechanism has been isolated.

## Code and maintenance cost

Actual release production-library executable text falls 66 bytes on Zen,
63 bytes on GNR and 48 bytes on V2; rodata is unchanged. V2 checked-get shrinks
144 →96 instruction bytes and its stack frame 256 →32 bytes. The
[local paired check](evidence/checked-point-local-20260911/provenance.json)
independently establishes the semantic and code change at generic ARM-O2;
its smaller local size delta is a different compilation context.

The source change removes two lines and adds one. Incremental candidate
compilation is recorded in each original worker receipt; the original single
TU compilation was not separately timed, so there is no compile-speed delta.
Archive file size grows slightly because debug/provenance bytes are included;
that does not contradict the executable-text reduction. The candidate does
not cure bound get16 range traversal or mutate the point-strategy contract.
