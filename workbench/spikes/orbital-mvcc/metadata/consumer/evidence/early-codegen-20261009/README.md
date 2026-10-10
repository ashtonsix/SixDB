# Early consumer codegen evidence

These are the original, byte-identical [cut1 receipt](cut1-receipt.json) and
[cut3 receipt](cut3-receipt.json), plus a compact [curation summary](summary.json).
They describe frozen experiments against `7f73c81`, not the current consumer or
selected runtime. No timing result or new qualification is implied.

The receipts retain ordinary LLVM return signatures, source/output identities
and, for cut3, actual record traits/sizes and hot-only object-section summaries.
The authored [cut1](../../codegen-control.md) and
[cut3](../../codegen-candidates.md) analyses retain the observed frames, red-zone
use, external-call limits and interpretation. Frame size is not type size or a
whole-program stack bound; smaller packets/code do not establish a speedup.

On 2026-10-10, a read-only preservation check matched all 23 candidate and five
output hashes for cut1, and all 28 candidate, 197 baseline and 40 output hashes
for cut3. The files still existed at the original local paths. The check did not
compile, execute, regenerate code or repeat the assembly analysis.

**This is not a complete archive.** The original documents' `build/` links and
the absolute paths inside these receipts identify local working material.
No shared full-source/output artifact was identified for these two captures.
The exact candidate overlays, full IR/assembly, object/sizeof binaries and
command files are not copied here. Their hashes identify bytes but cannot
recover them after local removal. The old baseline history has a separate
[recovery reference](../../../../baseline/history-artifact.json); that does not
recover these later private candidate overlays or toolchains. Current files
must not be silently substituted into an alleged replay.

What survives in Git is the historical analysis, extracted compiler facts,
source/output fingerprints and stated limits. Independently rebuilding the
exact experiments or rechecking every instruction requires the original local
captures or a separately verified recovery source. Original native/sanitizer
consumer checks and the later `shape-read-cut-1` are different captures; these
codegen receipts do not archive or requalify them.
