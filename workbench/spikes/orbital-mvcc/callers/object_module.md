# ObjectStore through a Loom Worker

[object_module.cpp](object_module.cpp) is a small actual caller of the extracted
`orbital::ObjectStore`. It uses the existing Shape material and codec, while
Orbital's `Representation`, `Part` and `View` own physical lifetime. Input bytes
come from `loom::allocate` on one configured Worker; no composed Material,
NativeFixture or second Lane/Pool is constructed.

The zero-Task-capacity control publishes and reads ready bytes directly. A valid
Task is refused without changing its frame. The two scheduled histories use a
stable, nonmovable reader frame containing a catalog Scope:

- Bind A and its current alternative, then park on a genuine Lane-owned Signal.
  A wake without readiness reparks the same Scope.
- Replace the selected alternative, then publish replacements for A and B and
  create a third Object in one accepted packet. A fresh Scope reads the new cut.
- Drain and reset the catalog while the reader remains parked. On normal resume,
  first discovery of B still reads the original cut, and the newly created Object
  is absent. The original A and the captured replaced alternative remain readable.
- Seal exact primary and alternative Part exports before ending the Scope. Both
  retain the original Object/version association. The selected-copy primary is
  decoded after its caller and Scope are destroyed.
- In the second history, detach and destroy the caller before physical completion.
  An independent record owns the alternative Part, cached immutable address and
  copied read law. A foreign thread reads only those bytes, then posts a retained
  completion Message. Its owner-Lane handler releases the Part through Loom poll.
  No foreign catalog or physical refcount operation occurs.

Worker shutdown must remain incomplete while that Part protects storage. Once
the foreign posting call is joined and the Message retires, the check requires
zero catalog/interpretation PMR bytes and eventual zero Arena charge. It also
checks empty Task/Message registrations before relinquishing the Worker Lane.
The metadata resource outlives the catalog, Scope, publication packets and Parts.

Build only `loom_object_module_check` with `SIXDB_SPIKES=orbital-mvcc`, then run:

```sh
orb -m ubuntu ctest --test-dir BUILD -R '^loom_object_module_check$' --output-on-failure
```

The target is included in `orbital_mvcc_metadata_validate`; the older
`loom_object_access_check` remains the separate rejected-baseline study. This
check reuses the selected material's existing cold catalog TU and interpretation
libraries. The fusion declaration header names its catalog and forward declarations;
Backend includes its Material directly, avoiding a legacy dependency for cold users.
It introduces no production API or Engine/container policy. Publications are
local completed cuts, not durability proof. This is correctness integration
coverage, with no performance or general concurrent-catalog claim.
