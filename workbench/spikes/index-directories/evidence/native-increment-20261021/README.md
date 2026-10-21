# Native directory and Segment increment

Selected short screens from21 October2026. [Implementation](../../IMPLEMENTATION.md)
explains operations, fields, ownership, methodology and limits. The selection
keeps current-source relocation losses alongside the common multi-row update
cases, plus release/sanitizer and full-slot qualification. Loop means are not
request percentiles. Only memory-only native LMDB is current; Calico has not
been rerun. The full ordered directory and complete memory accounting remain
unfinished.

The [recovery reference](artifact.json) retains the exact source capture, five
ELFs (including the ASan Segment caller), compiler/CMake commands, all raw local
screens and diagnostics, and `context.json`. Recover that context for individual
runtime-source/binary SHA256 hashes, CPU/OS/toolchain/library information and
flags. The full source manifest is intentionally kept in the archive.

Captured source digest:
`552c757a6f7b5c9c31af3553680976671be0aaaa814a1420e427437e6efc65b7`.
The ten runtime source/CMake files were checked byte-for-byte against that
capture before retaining the tested binaries. Later prose edits do not change
that executable source identity. Source capture includes unrelated live files;
only the reported Engine paths and linked native dependencies form this witness.

`multirow-*` uses actual base Segments with multiple64-byte records and all five
summary layers. Field3 updates publish ordinary Orbital COW correction-page
Objects; all directory root versions stay unchanged. Native LMDB uses u64
INTEGERKEY and one cursor with CURRENT|RESERVE; duplicate final intents are
coalesced, snapshots/output match, and Engine maintenance is inside write clocks.
`final-relocation*` forces fresh independent64-byte Segment material plus a
changed locator/root and one joined packet, with native u32 peer keys. These
are different semantic treatments and are not interchangeable performance claims.

`sanitize-full-slots.txt` is a correctness run, not a performance sample: two
Segments each span all65,536 physical slots; the old/fresh full-row and all-cell
aggregate oracle runs under ASan/UBSan/leak detection. The normal checks also
cover sparse/MAX directory prefixes, layers, refused/discarded/aborted private
updates, atomic union paths and independent physical lifetime. `oracle-check.txt`
qualifies the preserved API-free logical row workload separately from storage.
