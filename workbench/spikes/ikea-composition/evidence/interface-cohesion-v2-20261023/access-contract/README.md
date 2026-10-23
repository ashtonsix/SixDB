# Explicit TuplePack read access

The ordinary active-unit-only contract is restored. Native adapters and bodies
can separately select `author::read_access::common_window` when their owner
admits the initialized, readable and read-isolated first-to-last selected hull.
The choice adds no handle fields or runtime mode branch. Sparse stores and
journal effects remain selected-unit-only, including preserving writes that
read old contents under the stronger grant.

The [original NEON study](../../../../neon-tuplepack-masked/README.md) explicitly
permitted discarded inactive interior reads and measured useful sparse-window
wins. Public TuplePack comments instead promised no inactive payload access.
Allocation extent and a stride hint alone did not establish the stronger
initialization or isolation premise. No current Frame/Hashtable packet consumer
requires that silent widening; Hashtable's [TuplePack projection](../../../../../../ikea/include/ikea/hashtable/detail/representation.h) uses a one-row
GPR transfer. Keep the implementation available to owners with the actual grant.

## What the physical check proves

The [owning check](../../../../../../ikea/test/tuplepack/access.cpp) uses the
Rows32/two-byte and Rows64/one-byte shapes that entered the promoted NEON path.
It poisons inactive interior bytes within the valid allocation, separately from
prefix/suffix guards. It checks buffered and native reads, single-code predicates,
preserving writes, empty masks, stride-zero independent addresses and named
source/plane effects. The explicitly granted path receives initialized interior
storage, while its exact first/last hull and active-only stores remain checked.

The [receipt](receipt.json) retains all commands, source hashes, compiler, object
and binary identities. Optimized NEON checks pass with warnings treated as
errors. Every one of the nine TuplePack owning sources is freshly rebuilt for
both sanitizer arms:

- [Current strict/granted result](asan-run.log): ASan and UBSan pass.
- [Restored old gate](old-promoted-asan-run.log): the same check fails with a
  16-byte read of poisoned inactive interior, through ordinary Rows32
  `get` → `read_packet_buffered` → `gather_bound`. The one-line counterfactual
  patch is in the receipt. No uninstrumented old packet object is reused.

This is physical-access evidence that the earlier initialized-value or suffix
checks could not supply. Sanitizer build/run times are operational records,
not performance measurements.

## Preserving the useful native body

The [codegen witness](codegen.cpp) keeps strict and explicitly granted reads,
full reads and preserving writes visible. On NEON the current granted bodies
have the same literal instruction payloads as the old promoted bodies; moved
constant-pool references resolve to identical 16-byte values. Strict bodies
intentionally remove the sparse-window relaxation. AVX2 strict/granted payloads
and resolved constants match, and AVX-512 records match byte-for-byte with their
relocations. Cross-profile API syntax also passes. These are unlinked object
observations, not a new throughput or linked-placement qualification. A later
[comment-only source delta](qualified-comment-delta.json) maps the frozen proof
to the current header without changing executable tokens; the original receipt
is preserved.

The maintained TuplePack benchmark adds explicit `half/window` cases for the
owned, fully initialized tight64-row image. Existing unqualified cases use the
strict contract. The [owner addendum](owner-validation.json) qualifies the actual
narrow benchmark shard on all three profiles. Its [case list](owner-listed.txt)
contains exactly four window variants, and each passes one native iteration.
That loop is functional preflight; the separate access check supplies the
independent payload/effect oracle. Historical implicitly widened sparse timings
must keep their original permission when compared with either route.

The [reference](../../../../../../ikea/docs/tuplepack/reference.md) owns current
usage. Frozen R4 and the earlier compact-predicate diagnostic retain their own
source cuts; this correction does not retrospectively qualify them.
