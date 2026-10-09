# Shared physical shape candidate

This is a disposable alternative to the [descriptor control](../descriptor/).
It shares an admitted format and Part directory across physical realizations.
The catalog owns Object identity and logical cuts. A realization owns its record
count, body addresses and actual body ownership. There are no per-realization
Object labels, portable descriptions, decoded Schema objects or per-Part boxes.

`prove_fixture` admits the actual parent and every supplied Part through the
existing Ikea consumer. Tuple preparation then extracts four read controls from
the actual Schema and retains its exact 75-byte portable description. Columns
share derived geometry; `prove_series` independently decodes every supplied
40-byte description, requires one count and unique IDs, and admits every body.
This covers the existing four-field fixture and arbitrary counts of tight,
nonzero-width, headless Local SeriesPack Parts. It does not support all Ikea
encodings. A ProgramId denotes a caller-established semantic program: equal
program and count permit a conversion candidate, but do not prove equal content.

A realization retains a 64-bit count. Count plus admitted geometry reconstructs
each exact 64-bit body extent, with overflow checked; an additional per-body size
is redundant for these formats. Cold Series interpretation reconstructs the
original description, including count. Native Selection and View are trivial
8-byte borrows. Part resolution returns a 16-byte result, and count/proof control
is 16 bytes. LLVM IR on local AArch64 returns both pairs in ordinary registers.
Their validity comes from the protected catalog scope, not from per-read pins.

`shape_private.h` owns the actual Shape/Realization layout and inline addressing
helpers. A composed native consumer may deliberately depend on this private
header to fuse access with its kernels in an ordinary, independently compiled
TU. This exposes a maintenance dependency without copying layouts, expanding
the public API or textually including one implementation file in another.

Measured request sizes on 64-bit builds include complete headers and arrays:

| Record | One Part | Four Parts |
| --- | ---: | ---: |
| Shared shape | Tuple 227 B | Columns 184 B |
| Realization, independent shared owners | 48 B | 120 B |
| Realization, one real shared owner | 48 B | 72 B |
| Realization, independent Allocation owners | 40 B | 88 B |
| Realization, one real Allocation owner | 40 B | 64 B |

Shape is 56 bytes plus 32 bytes per Part. Tuple's 227 bytes include its 64-byte
read law and portable 75 bytes. Realization is 24 bytes plus address/owner entries.
There is no interning table: callers explicitly share an admitted Shape. Its
construction uses charged temporary geometry storage; large ID/Allocation-owner
sets use charged sorting storage. Small sets use bounded scans. PartId lookup is
linear; known Part positions avoid it. None of these sizes includes the catalog,
history capacity, allocator state, external payload or external owner controls.
The complete comparison must add those costs. Four independent `shared_ptr`
owners already cost 120 B per realization before any catalog state, so this
profile cannot plausibly meet a 128 B/Object complete budget.

Grouped shared ownership is admitted only when the supplied controlblocks are
actually identical, including aliasing owners with a null pointee. Grouped
Allocation ownership requires the actual same Allocation. Independent ownership
is supported separately. Every native Allocation establishes a genuine full-range
Hold before any original is released. Refusal drops all temporary Holds and
preserves every original. Success costs one retain/drop pair per physical owner;
export duplicates the live Hold token through the real allocator. The stored
8-byte token does not remove the allocator's Storage/region/Arena state. A grouped
escape retains the entire physical allocation, including when the selected Part
is the last 8 bytes of a 32-byte allocation. Capture and export run on its owner
Lane. No writable aliases remain after capture.

The catalog's sealed cold BoundSelection associates old bytes with their exact
Object/cut and supplies `export_part`. The internal `detail::retain_part` is a
fixture forwarding primitive, not an API for assigning logical labels. Export is
80 bytes including its identity snapshot and selected backing owner. Shared-shape
escape also pins the whole shape directory. Selected-copy escape copies only the
selected geometry, optional Tuple law/description and shared dependencies into
the same export allocation. After a 17-Part parent ends, the measured retained
metadata is 680 B with shared shape versus 168 B with selected copy. Both release
sibling bodies. A fixture's selected body/controlblock plus dependency control
retains another 104 B, separately charged. Physical region retention can remain
larger than the selected byte range.

Shape, realization and export use one caller-owned PMR resource, which must
outlive them. Capture rejects a foreign shape resource; the catalog must likewise
reject foreign realizations or charge them explicitly. Metadata reference counts
are single-owner and destruction is synchronous. This is not a concurrent
publication or reclamation contract.

`check.cpp` uses unconditional checks, including Release with `NDEBUG`. Native
and ASan/UBSan/LSan runs cover 0/1/4/17 Parts; exact old metadata; Schema and source
description death; real row/column reads for 0/3/9 records; selected body and
dependency lifetime; actual grouped owners; capacity and invalid-enum refusal;
allocation transfer, late wrong-Lane refusal, successful retry and complete
allocator teardown. The catalog's separate checks cover issued binding across
publication and conversion. No composed performance result is implied here.
