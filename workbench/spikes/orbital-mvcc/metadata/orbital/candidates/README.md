# Compact history candidates

These are embedded, single-owner comparison backends over real immutable
descriptors or shared Ikea shapes and externally owned encoded bytes. They do
not implement Journal, IO, concurrent reclamation or an Engine index.
`candidate_store.h` owns the common protocol; `history.h` and `inline_history.h`
own the alternative storage layouts. No API here is promoted.

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
  auto issued = store.bound(read, object);
  auto export_owner = issued->export_part(0);
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
terminal no-effect result. Abort keeps the single-writer exclusion until every
change has released its entry pin and private owner, including through reentrant
destructor callbacks. Install neither allocates nor destroys user owners.
Abort remains exclusive until every accepted entry and private owner is released;
disposal callbacks can drain, but cannot prepare another batch mid-abort.

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

`Store<Nodes, Retention, Physical, Objects>` keeps the controls explicit. FloorRetention
keeps all history down to the oldest reader; DistinctRetention keeps the selected
predecessor for each distinct active cut using charged temporary scratch. The
exact policy can remove an unpinned sole tombstone even with older absent readers.
Replacement alternatives are captured only when discovered by a retained Scope.
`shape_policy.h` shares proven Ikea geometry across physical realizations. Its
logical binding lives in cold catalog-issued targets/exports, not the body.
`bound(scope, object)` seals the selected physical value and exact Object/cut into
one immutable borrowed pair. Its `export_part` creates an escaping owner; the
issuing Scope must remain live until that call. A copied pair preserves its
original association through later publication, deletion and recreation.

`HashObjects` retains the original node-hash control. `CompactObjects` stores
full-width Object IDs once in stable 24-byte paged entries, with 32-bit lookup
buckets. A charged cold vector tracks reclamation debt; its index uses existing
entry flags. Prepare reserves new debt slots before acceptance. Reentrant debt
compaction stops/replans an active sweep even when no publication occurred.

`InlineShapeStore16` puts the current cut/physical pointer directly in a 40-byte
Object entry. Only replaced tips require old16 history and descriptor slots.
Prepare borrows the pinned current owner; install transfers it into history and
the accepted replacement into the tip. The old link is read again at install
because a drain may have shortened history. Alternatives use exact entry/cut
keys, so adoption between prepare/install requires no install migration. Entry
pins also preserve debt slots until completion, keeping install allocation-free.

`Memory` reports requested PMR bytes, peak and allocation calls, not malloc
overhead or resident pages. Add its 40-byte caller-resident resource once to a
whole-state account. Statistics include Core/entry/node sizes, reserved history
and descriptor/Object-directory/debt capacity, metadata copied during capture, requested
descriptor bytes and reclamation work. External encoded owners/control blocks
and payload allocations are separate costs, not free memory. None of these
figures establishes database performance; use the composed driver for that.

`check.cpp` exercises eleven grouped histories for all three node layouts, both
retention policies and both directories plus inline16, including coherent late lookup, absence/recreation, private
reads, refusal sweeps, alternative replacement, narrow export, Store teardown,
conversion authority and reentrant owner disposal. `shape_store_check.cpp` adds
real encoded rows/columns, repeated physical values at different logical cuts,
sealed exports, foreign-resource rejection, delayed conversion and accepted work
crossing old-history reclamation. Checks run under NDEBUG. `abi_probe.cpp` checks
ordinary caller packets; it does not replace end-to-end measurements.
