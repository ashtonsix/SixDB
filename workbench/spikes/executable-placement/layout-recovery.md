# Checked-point layout discriminator

The [retained result](evidence/checked-point-layout-20260911/summary.md)
tests whether restoring linked placement changes an otherwise unchanged bound
point control while preserving the simpler checked getter. It records the
positive result, the remaining difference and the limits of attribution.
Nothing here selects a production padding policy.

`run.py` reconstructs the original V2 caller objects and benchmark dependency
under their original paths, requiring every recorded object/archive hash and
byte-identical baseline/candidate relinks. It appends 48 unreachable NOP bytes
to the candidate getter's ELF text section, preserving its executed instructions
and unwind information, then checks restored surrounding text and rodata.
Three existing semantic checkers precede four witnesses in reciprocal
base/candidate/padded/padded/candidate/base order. A mismatch stops before timing.

The successful job is `20260911T070108Z-f9a5ebd2`, capture
`2b63964ee5bf6289ab689bfdbd2ef6ee8ce90440e6e6eeab58765e5bd4d900b6`.
The source archive in its worker bundle owns the exact measured script; this
README was added after measurement. Full binaries and sources are recoverable
through the two worker references in the retained evidence. The failed first
preparation and corrected recovery destination remain identified there.
