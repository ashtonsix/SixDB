# ARM single-stream completion dependency

The captured complete Hashtable caller had an approximately 35 ns gap between
hinted single-stream Ring and manual lookup on Neoverse V2. Its ready callbacks
were already frameless and call-free. The remaining problem was the dataflow
from a terminal miss into its successor request.

In the slow generated bodies, LLVM reused a loaded descendant count, known to
be zero on the taken miss branch, as the later `found=false` value. A `CSEL`
then selected the next request key using that memory-derived value. Fast bodies
either materialized false independently or computed the miss key directly.
The predicted miss branches could then permit successor work without a data
dependency on the current bucket. This is a dataflow interpretation of the
instructions and controlled timings, not a measured stall or branch-prediction
breakdown. Descendants usually share a demanded tag line; an extra independent
header DRAM read is not the explanation established here.

All eight inspected reference/snapshot H0/H1 Ring/manual shapes follow this
pattern. The original aggregate-reset H0 body does too. Primary-only probe
preparation removes unused cold-field stores, but also changes this lowering;
its initial unhinted recovery cannot be attributed solely to saved stores.
Ring lane pairs have identical producer/consumer widths in the fast and slow
cuts. Manual C1 keeps that state in registers, so a changed pair-forwarding
explanation cannot account for its reversal.

The causal panel fixes keys, values, layout, seeds, eight turnovers, request
recurrence, hint windows, policy, native Ring and CPU0 on the same on-demand
`c8g.large`. These are three-repetition reciprocal-throughput medians in
ns/request, including complete operation preparation and drain.

| Aged C1 control | Manual with hints | Ring with hints |
| --- | ---: | ---: |
| Primary-only prepare reference | 137.397 | 172.888 |
| Consumer hit/miss branch | 138.232 | 138.289 |
| Independent false register value | 138.498 | 139.272 |
| Reference ELF, PRFM replaced by NOP | 144.774 | 183.294 |

The branch control closes the gap against both its own manual and the fixed
reference manual. The independent-false control supplies a separate dependency
test. The NOP control changes exactly 2,382 four-byte instructions and preserves
all other executable bytes, addresses and branches. Its persistent C1 gap rules
out needing a hardware-prefetch interaction to explain the original result.
At C32 it costs 84.783 ns instead of the reference's 34.556 ns, confirming that
the hints remain useful in this comparison. Logical potential hint targets
remain equal even when the actual instructions are removed.

The branch control retains aged C32 hinted Ring throughput at 34.544 ns versus
34.556 ns reference. Hot hinted Ring improves from 18.144 to 16.740 ns at C1
and from 13.876 to 12.740 ns at C32. A broader four-field register snapshot
had also made hinted C1 fast, but regressed unhinted C1 and other modes; it is
a negative control. Singleton-frontier and store-before-prefetch controls did
not close the gap and are not selected.

The ARM repair expresses the application's two terminal outcomes:

```cpp
if (found) {
    job.sum += value;
    ++job.found;
    job.key = value;
} else {
    job.key ^= missing_bit;
}
--job.remaining;
```

Each captured caller initializes `value=0`, and a terminal miss leaves it
unchanged. Skipping that zero update gives plain C++ the desired native branch
shape without assembly, branch weights or a new API. All 20 broader plain-form
release ready callbacks have no `CSEL`, ordinary calls or stack accesses. Mixed,
ragged, long-chain C1/C8/C32 controls pass complete answer and exact Ring-visit checks
under release and O3 ASan/UBSan with always-on stack-use-after-return detection.
The final V2 panel uses seven repetitions in each of two opposite execution
orders. Aged C1 hinted Ring changes from 172.847 to 138.488 ns; its matched
manual is 137.618 ns, leaving 0.870 ns. The two Ring pass medians are 138.473
and 138.503 ns. Aged C32 hinted Ring changes from 34.743 to 34.270 ns, and fresh
C32 hits from 31.614 to 30.570 ns. Hot hinted C1/C32 also improve. The broader
plain form regresses hot unhinted C32 by 0.479 ns (4.63%); that negative remains
in the complete panel. The reproduction therefore selects this repair for
hinted ARM work and preserves the reference expression for unhinted work.

The same plain spelling is rejected as a portable change. On Zen5 AVX2 its
aged C1 hinted Ring regresses from 143.813 to 186.954 ns, with the manual still
143.493 ns. C32 remains close, but that does not excuse the singleton loss.
The original x86 miss already materializes false with an independent `XOR`
before its conditional move; it has no descendant-count dependency. Removing
all conditional moves still regresses, with no new calls or stack accesses.
Keep that existing x86 lowering. This is not a general rule against selects,
and the Zen reversal does not establish a particular stall mechanism.

Ikea's current prepared reader already avoids this dependency. Its captured
Fuller V2 C1 hinted times are 137.778 ns Ring versus 138.346 ns manual for hits,
and 118.744 versus 118.195 ns for mixed requests. Its 28 ready callbacks compute
the miss key directly, with no result-key select, ordinary call or stack access.
The larger mixed comparison against Abseil is a kernel comparison; it is not a
remaining Loom/manual gap. Preserve that current boundary instead of imposing
another speculative repair.

The public Ring signature, Slot layout, shared prefix, exact budgets and
handoffs stay unchanged. Ikea owns the prepared lookup and its consumer;
Loom supplies the common scheduling and policy machinery. The finding is to
inspect the successor's critical dataflow alongside argument traffic and
frames when qualifying a caller, rather than inferring overhead from store
counts alone.

The narrowed reproduction is commit
`ff6a6b4f65b8a4def069a400d3b9e91532b55294`, applied as a patch to the final
frozen capture. Pinned target preprocessing preserves x86's reference
declaration, body and calls exactly. A local AArch64 comparison covers 95
functions: ready and manual unhinted work plus fused lookup match the reference;
hinted work matches the measured plain form. Instruction/register/control
sequences and padding counts match after address normalization. The 45 hinted
manual differences are relocations of the same decoded cold error string.
This is source/codegen preservation proof, not a newly measured combined panel.

The original capture is digest
`7a2b737ed98881381953f855145c34060ce47c8f6d340f9c5fc37105ca8cfed9`.
The qualified causal worker is `20261013T123321Z-920f324c`, source digest
`5ce8d4a3539c92fa745dd592cc30165f0b9131e1732c20ba1dd2ab558572917e`.
Current Ikea is worker `20261013T123020Z-1f4ae28f`, digest
`aafa5a315d52c661d31ec8bcc94085216a8d8224c165f4c050a0f0e0b4e8c9d0`.
Final plain-form workers are `20261013T125940Z-23733a42` (V2) and
`20261013T125940Z-948084f1` (Zen5 AVX2), both digest
`44e5268eeafa03688a3f300afd12a4fe488c9b9698b35385f8116ad08d30714f`.
The selected evidence retains complete samples and the small instruction
receipts; full binaries, disassembly and frozen sources remain recoverable
from their indexed worker archives. Different source cuts are not pooled.

The [recovery index](evidence/arm-20261013/recovery.json) keeps all 2,478
campaign rows and the separate 588-row current-caller reference, including
the rejected portable and unhinted controls. The checkout-owned report verifies
every selected payload before recomputing all samples, medians and comparisons.
From the Linux repository root (prefix with `orb -m ubuntu` from macOS):

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/call-boundaries/ring-dispatch/evidence/arm-20261013 \
  build/reports/loom-arm-single-stream
```

Recover a final worker's indexed `source.tar.gz`, then apply the retained
[patch](evidence/arm-20261013/qualification/local/hinted-selected.patch) with
`patch -p1` to reproduce the narrowed selector. The
[proof](evidence/arm-20261013/qualification/local/hinted-selector-proof.json)
records its expected consumer hash. This retains one lookup
implementation and selects its consumer at compile time; it does not add a
runtime architecture query. Timings establish the measured V2 policy (code 1),
not every hint code, pressure level, compiler or ARM model.
