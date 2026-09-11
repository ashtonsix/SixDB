# SeriesPack measurement history

The recurring [workload definitions](../measurements.md)
and [runner](../README.md) now live with the benchmark
suite. This note preserves the early implementation lessons; dated hardware
findings and follow-ups are indexed [here](../../../spikes/ikea-composition/validation/README.md).

## What the first regressions changed

Correctness coverage and wrapper/direct equivalence advanced ahead of a
competitive kernel baseline. That was a review error: identical wrappers and
direct controls can share an inefficient physical algorithm. The broad width
matrix established semantic coverage, but did not justify spreading a choice
of batching or input-carrier handling across it. Targeted matched screens must
inform substantial kernel adaptations while they are still cheap to change.

The initial comparison also overemphasized Calico and omitted direct
LocalPack/ScanPack controls. That discarded relevant knowledge from the
immediate predecessors: LocalPack already had a 64-value weighted one-bit
encoder and specialized one-/two-bit decoders, while ScanPack used ordered
field insertion to avoid redundant masks. Reintroducing these mechanisms is
recovery of prior work, not a newly discovered algorithmic improvement. The
new direct controls make those losses and subsequent adaptations visible.

The preliminary Apple/Orb NEON screen exposed two concrete mistakes in encode.
Striped packing chose its work size from the input element width: u64 sources
made a 16-byte SIMD tail operation process only two values per pass. It repeated
field combinations and tiny stores instead of narrowing a larger source region
in registers and using the byte lanes. Local width 1 used the general eight-plane
transpose even though it only needed one output plane. A weighted low-bit
reduction removes that extra work. These are physical-kernel losses; neither
requires changing the wire format or adding a continuation interface.

The first paired correction screen brought Local1/u64 and the supported striped
u64 encoders ahead of the specialized prior on this host. That is not yet a V2
result or complete acceptance: narrow u8 input still exposed losses. A further
Local1 experiment keeps the eight-value data tile while processing 64 values in
a dense internal region, with exact pair/single remainders. It tests the earlier
composition concern directly: logical tile size need not dictate a fine internal
handoff or force a reduction/store after each small producer invocation. Boundary
and arbitrary-high-bit projection checks accompany that region change.

The initial Local1/u64 observation and the later paired baseline differ materially
(0.251 versus 0.151 ns/value), despite verified identical kernel source provenance.
The cause is not established. Improvement claims use the paired before/after
run; the first screen remains a regression signal, not a number to splice into
that comparison. Keep per-case source, carrier, target and call-grain context,
and repeat weak/noisy cases rather than accepting a favorable aggregate.

The x86 hardware check also found a separate contract error. The head adapter
promised a per-lane right shift but reused a word-shift helper whose byte-field
callers must mask neighbor bits. It omitted that mask for u8 sources. The old
independent oracle used carriers large enough to represent K and missed this
valid narrower-source case; the public contract test caught it. The adapter now
supplies the mask, and the independent oracle covers all supported unsigned
carrier widths, including ones narrower than the destination domain. A shared
helper's access and bit obligations must survive each adaptation, not merely
its nominal C++ vector type.

## What later boundary measurements changed

Large losses persisted for different reasons at different layers. Useful facts
such as range shape and output type were available too late to a broad runtime
endpoint; packet size constrained execution grain; and scalar finalization
repeated after each small producer. Those choices were not obligations of
substitution. Separately, missing physical optimizations and target-dependent
compiler lowering caused losses even in direct paths. A composition explanation
must distinguish those causes instead of assigning every residual to abstraction.

The scalar reader callback carried an output aggregate containing information
unused by the trusted kernel. Passing its pointer and element width separately
recovered substantial ordinary get16 performance on both x86 targets while
retaining checked capacity semantics. The ARM gains were smaller and some cases
regressed. This supports changing the boundary, not a universal call-cost model
or a claimed store-forwarding mechanism. Admitted and raw controls can also
exchange places after relinking, so their timing differences are not independent
overheads that can simply be added or subtracted. The
[admitted-range findings](../../../spikes/seriespack-range-execution/admission.md) retain target and workload scope.

The [grouped composition results](../../../spikes/ikea-composition/native-regions/spectrum.md)
separate physical layout, execution grain and result representation. The same
authored operation over independently bound children beats the measured
materializing alternatives in the initial widths 12/31 comparison after grouping
reads and deferring finalization. The later dense all-width screen puts the
grouped deferred median first in all 48 width/mask groups on each x86 machine;
two Zen comparisons overlap and remain ties. Grouping alone has a different outcome across
targets, so the result choice remains explicit. Existing native bodies can be
curated without losing those compiler lowerings, as the subsequent byte/relocation
comparison shows; an improved materializer still needs a fresh comparison.

The practical review lesson is to assess both the kernel and its real enclosing
operation against the strongest relevant control while adaptations remain easy
to change. Correctness, shared source, wrapper/direct parity and aggregate wins
answer different questions. Keep material per-case regressions visible and
investigate their causes; none of those observations establishes that composition
makes a gap unavoidable. The current
[performance and maintenance direction](../../../spikes/ikea-composition/design.md#performance-and-maintenance)
allows an understood residual primitive gap when the simpler implementation and
useful compound consumers justify it. Primitive parity is not a prerequisite
for continued development, and compound recovery must be measured before it
is claimed.
