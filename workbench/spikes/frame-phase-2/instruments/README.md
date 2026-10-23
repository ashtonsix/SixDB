# Instruments and baselines

Ikea LEAD owns front A and Ikea-side change support; Claude owns the execution
substrate. These instruments support complete Frame comparisons, not a separate
performance target for the module.

The [calibration findings](FINDINGS.md) retain the fixed-work caveat, corrected
common-window Zen 5 rates, PMU limits and the smaller-instance experiment plan.
[Native codec controls](codecs/README.md) establish pinned functional baselines;
their boot checks are not speed comparisons.
The [ALP IEEE audit](alp-audit/README.md) supplies a tested complete RD path and
retains automatic decimal ALP failures at the pin. Decimal boot round trips do
not qualify a general-IEEE control.
The [change witness](../change/README.md) establishes pre-payload read/store
admission for tested fixed-basis writers using current Ikea primitives. It
keeps logical contribution and actual physical effects separate, without
selecting a concurrency policy or changing query paths.

The [phase-1 scans](../../frame-phase-1/scan/FINDINGS.md) supply exact native FOR
and actual Polars controls, balanced treatment order and actual-buffer conditioning.
They also retain the important scattered-cold loss. The
[memory study](../../memory-characterisation/FINDINGS.md) supplies validated
independent-chain curves, topology/page receipts and the distinction between
random demand concurrency and trained streams. Its adjacent-prefetch history
has a separate corrected study; none of that old training is used here.
[Residency tools](../../../tools/residency.md) record the counter sensitivity
limits: advertised/opened events need not respond to real misses.

The missing measurement is concurrent sequential and random read throughput,
from one physical core through a cache domain and the available guest CPUs.
`frame_bandwidth_probe` measures useful read work, with private mappings first
touched by their pinned worker. Random reads use independent pointer cycles,
not an extra timed index array. Every cycle covers its entire assigned line set;
independent expected final positions and complete edge validation check the
constructed trace. Sequential reduction consumes every u64 and checks its
modulo-u64 answer. Timed kernels compile separately from repetition orchestration.

Repeated work, declared footprints, actual mapping/page placement, synchronization
and per-thread intervals remain visible. Addressed-line bytes for a random load
are a nominal cache-line demand, **not measured traffic**. A guest-wide plateau
is not a socket-bandwidth claim. No cache tier is inferred from a size or a
prefetch instruction. The first calibration is read-only; output/RFO/writeback
and actual Frame/peer operations remain additional measurements.

The first large Graviton fixed-work cohort exposed unequal finishing times:
in its worst 96-core large scan, all workers started within 0.3 ms, but their
durations ranged from 16.6 to 64.6 ms. That is a valid completed-batch rate,
not a clean steady-state saturation interval. The `--steady` variant therefore
uses one common deadline and bounded chunks, retaining each worker's actual
completed units, timed line population and intervals. Independent oracles run
after every worker has stopped its timer. These two measurement boundaries
remain separately labeled; the new mode does not rewrite the first results.

Downloads are already authorized. Inputs larger than 1 GB use small local
slices and larger worker-side slices. The peer and corpus work follows the
operation families: native controls and actual Polars, integer codec peers,
native system-query boundaries, floats, bitmap and reorder controls. Landing
every peer is not a prerequisite for the first substrate comparison.

The bounded IEEE input recipe is `corpus.py`: by default it prepares 65,536
original rows for 14 Jena weather columns and two ALP fixture columns, 8 MiB
of binary64 payload. It uses the existing dataset cache and records source
identity, numeric lexeme hashes, original order and first/last IEEE bits. It
does not scale, quantize, remove the Jena -9999 sentinel, or invent time-series
boundaries for the already flattened Shapes fixture. Use `--help` for source
and row bounds. Larger inputs belong on workers; a prefix is a forcing input,
not a representative population sample.
