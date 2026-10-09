# Origin-only Shape with Direct4 Allocation

This companion adapts the [origin-owner control](../origin-owner/README.md) to
the actual [Direct4 runtime](../../../allocation/routes/direct-domain/README.md).
It contains only Shape changes: private address routing, checked export cleanup
and focused falsifiers. Allocation implementation and patch history remain with
their owner.

`allocation_origin` loads the packed use-state once and passes that snapshot to
`detail::route`, reusing its immutable high-byte identity for
`FiniteGroup::resolve`. Window lookup uses the same actual Domain route. Holds
still prevent rebinding and retirement. This inherits Direct4's pinned
Clang/Linux enclosing-object arithmetic limitation; it adds no portable-language
claim or new Allocation API.

Export now propagates checked `retain` refusal and deallocates its unexposed
metadata before returning. This is the existing checked-owner behavior rebased
over origin-only Shape; **do not additionally apply `routes/checked-owner.patch`**
to this consumer. The focused check injects saturation at identity 255 and on a
Window, verifies both export policies preserve metadata, count, identity and
bytes, then retries each successfully.

The 8 B per-Part saving and 8 B Selection/View packets are unchanged. Reads now
touch the packed counter word as part of address recovery. No timing winner,
additional metadata saving, sanitizer or whole-catalog qualification is claimed.
The separate fusion control remains uncombined; its read seam must use
`part_address`, not the explicit-layout-only `address_slot`.

## Reproduce

Use an isolated current Shape source tree with the runtime files identified by
[the Direct4 cut](../../../allocation/routes/direct-domain/direct-domain4-cut.json),
recoverable through its [archive](../../../allocation/routes/direct-domain/artifact.json).
Alternatively construct that runtime through its documented Allocation patches.
Keep the consumer unmodified by the older checked-owner patch, then apply:

```sh
git apply workbench/spikes/orbital-mvcc/metadata/orbital/shape/controls/origin-owner/origin-owner.patch
git apply workbench/spikes/orbital-mvcc/metadata/orbital/shape/controls/origin-owner-direct4/direct4.patch
```

Configure with the pinned Linux toolchain and `SIXDB_SPIKES=orbital-mvcc`, then
reuse that build:

```sh
orb -m ubuntu cmake --build build/origin-direct4 --target \
  orbital_mvcc_shape_origin_owner_check orbital_mvcc_shape_check orbital_mvcc_point_write
orb -m ubuntu ctest --test-dir build/origin-direct4 \
  -R '^orbital_mvcc_shape_(origin_owner_check|check)$' --output-on-failure
```

Both native checks pass with Clang 21.1.8, AArch64 Linux, `-O3 -DNDEBUG` and
NDEBUG-active checks. The actual point writer, Shape writer and read/bridge TUs
also compile. Qualification reuses the unchanged, previously qualified Direct4
runtime library, with all runtime input hashes checked against its owning cut.
It does not rerun Allocation's broader native or sanitizer campaigns.
