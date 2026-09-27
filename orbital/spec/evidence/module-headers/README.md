# Purpose headers and checked sources

The 67 modules without a concise introduction now have a short `(* ... *)` block
immediately after their `MODULE` declaration. The other 32 introductions are
unchanged. Every original byte remains in place after the insertion.

[audit.json](audit.json) records the exact inserted text and before/after hashes.
Its baseline is Git commit `44a0234092998b8050b37ff822d39d90cfca5585`, whose 711
selected source identities match the [retained 613-case evidence](../admission-renewal/README.md).
[verify.py](verify.py) reconstructs each annotated file from that baseline,
rejects any other change or embedded comment delimiter, and checks all remaining
model/configuration files, case catalogs, runners and the TLC pin. From the
repository root on Linux:

```sh
python3 orbital/spec/evidence/module-headers/verify.py
```

The audit passed, and the pinned SANY parser accepted all 99 maintained modules
with semantic analysis and level checking enabled. The parser was invoked from
`orbital/spec` with the pinned `tla2tools.jar` on its classpath:

```sh
java -Xmx512m -cp /absolute/path/to/tla2tools.jar tla2sany.SANY -error-codes *.tla
```

The jar's SHA-256 is
`ab4694601923fd5ac06452abbf847c366a5054a3d739552085edd6ed986c29ec`,
as recorded in the [tool pin](../../../../workbench/tools/tlc/source.json).
Prefix Linux commands with `orb -m ubuntu` in the macOS workspace.

No state exploration was repeated. Original receipts, source captures and
archives are unchanged; their results describe the pre-header bytes. This audit
establishes only the stated insertion, not arbitrary comment equivalence. The
ordinary collector still rejects affected receipts as exact-source mismatches.
Later executable changes require their own evidence and will fail this audit.
