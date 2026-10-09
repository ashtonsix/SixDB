# Compact history candidates

These are embedded, single-owner comparison backends. They use the real
[descriptor candidate](../descriptor/descriptor.h) and externally owned encoded
bytes; they do not implement Journal, IO, concurrent reclamation or an Engine
index. `candidate_store.h` is the usable adapter. No API here is promoted.

```cpp
compact_history::Memory metadata; // final address; outlives every escaping owner
compact_history::Store16 store(metadata);
{
  compact_history::Store16::Scope basis(store); // 32 bytes, caller-resident
  spec.version = basis.cut() + 1;
  auto rep = store.capture(spec, parts);
  compact_history::Change changes[] = {{spec.object, std::move(*rep)}};
  auto prepared = store.prepare(basis, changes);
  store.install(*prepared);
}
{
  compact_history::Store16::Scope read(store);
  auto selected = read.bind(object); // 8-byte borrowed physical descriptor
  auto part = selected.part(std::size_t{0});
  auto export_owner = compact_descriptor::retain_part(selected, 0);
  // Release export_owner explicitly; its metadata resource must still be alive.
}
store.drain();
```

The example elides error checks; `check.cpp` shows fallible calls. Ordinary
`Scope::bind` returns an empty Selection for absence. A Scope pins the real Core,
keeps future discovery coherent and survives Store move/reset. It is nonmovable
because the Core links its caller-owned address. A Borrowed scope ends before
publication, suspension or final Store reset. Native Views own nothing.

`prepare` accepts an immutable result against this Core's exact predecessor and
issues one private, single-use occurrence. Refusal leaves logical state unchanged.
Installed work cannot be installed twice; abort/destruction of accepted work is a
terminal no-effect result. Install neither allocates nor destroys user owners.

Conversion uses `conversion(scope, object)` to issue a cold capability, followed
by `adopt(target, replacement)`. It pins the exact Core incarnation and physical
source, **not** an invisible read cut. If history is no longer needed and has been
pruned, adoption refuses. Object/cut labels in a replacement cannot substitute
for that capability. The caller still establishes content equivalence.

The 32-byte node holds cut, default/alternative pointers and an older slot. The
16-byte node holds cut and two indices; its descriptor directory costs another
8 bytes per slot before capacity slack. The 8-byte node shares its cut across a
publication chunk; header/directory cost is about 24 bytes per chunk, and retired
holes cannot be reused for a different cut. Both slab candidates retain their
free pages for reuse. Slot generations are unnecessary here because no version
slot escapes; cold capabilities retain real owners and re-find logical history.

`Store<Nodes, Retention, Physical>` keeps the controls explicit. FloorRetention
keeps all history down to the oldest reader; DistinctRetention keeps the selected
predecessor for each distinct active cut using charged temporary scratch. The
exact policy can remove an unpinned sole tombstone even with older absent readers.
Replacement alternatives are captured only when discovered by a retained Scope.
`shape_policy.h` is a separate physical-profile experiment with shared geometry;
its logical binding lives in cold catalog-issued targets/exports, not the body.

`Memory` reports requested PMR bytes, peak and allocation calls, not malloc
overhead or resident pages. Add its 40-byte caller-resident resource once to a
whole-state account. Statistics include Core/entry/node sizes, reserved history
and descriptor-directory capacity, metadata copied during capture, requested
descriptor bytes and reclamation work. External encoded owners/control blocks
and payload allocations are separate costs, not free memory. None of these
figures establishes database performance; use the composed driver for that.

`check.cpp` exercises nine grouped histories for all three node layouts and both
retention policies, including coherent late lookup, absence/recreation, private
reads, refusal sweeps, alternative replacement, narrow export, Store teardown,
conversion authority and reentrant owner disposal. Its checks run under NDEBUG.
