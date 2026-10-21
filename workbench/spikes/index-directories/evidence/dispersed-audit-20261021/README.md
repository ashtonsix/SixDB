# Dispersed read attribution,21 October2026

[Context](context.json), [actual work counts](census.json), and
[CPU shares](cpu-shares.json) qualify the retained a5f6a1a8 executable. Two
software cpu-clock profiles at4999/1009Hz, zero lost samples, retain7698/1483
Producer::read-stack samples. Exact P/U instruction ranges were checked against
disassembly. Hardware cycles/instructions are unavailable. Shares normalized to
the earlier136.795ns give approximate attribution, not independent stage timings.

Engine directory admission/get/find occupies4.52–4.60% of those samples
(~6.2ns normalized). Summary search is30.0–30.6%; correction-cache lookup
27.5–29.1%; correction Object binding21.0–21.8%. The latter work belongs to the
test driver's Segment caller. Scope/selector/other, key refinement and row
consumption make up the rest. The older59.7ns diagnostic exercises J0 logical
seek on L4; it is a different path and cannot be subtracted from shared mode.

The setup has one occupied L2 hash entry and80 base dirents, resident/warm, with
no directory edits in its B16 data-update arm. It is a weak directory stress
case. Caller grouping/cache/compact-summary controls were explored, then set
aside at Ashton's instruction. Those source/ELFs, raw samples, ASan receipt and
profiles are retained for audit; they establish no directory speedup or
acceptance. Full setup/touches and the owning interpretation are in
[Implementation](../../IMPLEMENTATION.md#dispersed-case-directory-attribution).

The [verified recovery bundle](artifact.json) retains both raw software profiles,
exact baseline ELF/disassembly, classifier, caller-control source/ELFs/receipts
and frozen-parent source identity. Reanalysis may use the retained textual frame
dumps directly or register the exact baseline ELF with perf buildid-cache.
