# Canonical callbacks for equivalent point-reader strategies

Workbench-only candidate; no production installation or performance claim.

The one-function patch selects a single private callback when the existing arithmetic getter delegates to the constant-offset getter. Both strategies remain real choices for striped residual widths 3, 5, 6 and 7. The bound reader retains the requested `point_strategy`; source layout, placement, bounds and result semantics are unchanged. No API promises distinct function addresses for algorithm-identical callbacks. `assume_valid` still accepts custom endpoints without canonicalization.

The current ARM -O2 object contains 188 pairs with identical normalized instruction and relocation bodies. The candidate removes those duplicate point instantiations (412 to 224; 37,796 to 22,152 function bytes) and simplifies binding code. Total function-symbol text falls from 648,720 to 615,880 bytes. `cost-summary.json` separates linked executable text, constants and unwind data; the static-point-only checker does not retain the affected binding paths and does not shrink. These are compile/link observations, not a runtime or build-time speedup. The candidate compile's wall time is a single observation without a matched before build.

Before/after check binaries use exactly the same checker objects and archive members except operations.cpp.o. Both variants pass operations (including strategy metadata), static points (206 descriptions, 2,060 placements, 443,664 reads and 85,716 writes), and public-wire checks for scalar and NEON (each 206 descriptions, 5,532 placements, 83,787 range checks, 49,140 mutation checks and 206 append scenarios). See `checks.json` for commands, hashes and outputs. No new implementation-mirroring test was added.

The original and candidate sources, exact patch, objects, archives, compile/link commands, symbol deltas and binary identities remain in this ignored review directory. Other targets and runtime behavior have not been measured. Retain this as a low-complexity maintenance option; it does not repair the remaining bound short-range performance gaps.
