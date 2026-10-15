# Nonzero owner-counter stores: local mechanism receipt

The bounded diagnostic changes only the successful mutation tail: update
`owner_.keys` or `owner_.values` when its returned delta is nonzero. Returned
deltas and the caller event remain unchanged. Both variants already contain the
same partial-record mutation experiment; neither is a selected production policy.

In the **actual AArch64 K40/W2 map primary-hit paths**, the guarded variant removes
both owner-counter loads and stores. This holds for the six inspected recipes:
A6/A10 and arms 0/1/2. The partial control still loads and stores both counters
after producing zero deltas. `witnesses.txt` follows the successful equality edge
through the write tail to the next-key boundary; it is not a whole-function grep.

The inspected **MM A10/arm0 primary inline append** still stores its payload/tag
and increments the total-value count at owner offset `0x128`. Its unchanged key
count at `0x120` is no longer loaded/stored on that path. The explicit witness
assumes an inline tag of 0, 2 or 3 (W2, eight-byte inline budget); it does not claim
to classify every MM spill or overflow instruction path.

The copied source/DWARF audit resolves all 20 complete update intervals per binary,
including ordinary/exact Abseil, with no audit failures. Final binary, TU, generated
header and assembly hashes are in `mechanism.json`. Compilation used Clang 21,
`-O3 -march=armv8.2-a+crypto -mtune=generic`, with both auto-vectorizers disabled.

This is **instruction evidence, not a speed result**. The local experiment contains
10,800 rows from 240 successful processes; all 540 physical-inventory cells agree
across variants/repetitions. The unchanged Abseil controls varied between local
runs, so small timing deltas are inconclusive. No native-platform or causal-cycle
claim follows. Raw rows, process receipts, summaries, final binaries, complete
assembly, generated headers and audit outputs remain in the recoverable bundle.
The parent reported the W2 independent history check passing; this audit did not
rerun the checker or execute timed operations.

The base source snapshot in `source.tar.gz` has matching hashes for every original
spike header recorded by the compile receipt. Apply the relevant variant's copied
`promoted.h`/`record_table.h` when reconstructing it. `source-recovery.json` identifies
that base and the overrides. Toolchain binaries and fetched dependencies are not
embedded. The bundle preserves both binaries for inspection without rebuilding.

Recover the full directory using `workbench/tools/artifacts.py fetch` with this
directory's recovery reference. Then run `qualify-mechanism.py` in the recovered
directory to recheck the emitted witnesses, source/binary identities and raw-row
coverage. `audit-commands.json` records the original disassembly/DWARF commands;
`audit-dwarf.py` is the locally copied audit, not a modified shared native audit.
