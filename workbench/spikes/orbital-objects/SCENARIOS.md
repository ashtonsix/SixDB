# Durable objects: scenarios and counterexamples

2026-09-26. Analytical cases for the [current Orbital brief](../../../orbital/BRIEF.md),
not an implemented memory or recovery contract. They examine ordinary pointer
access and COW elision under fixed-position execution. The owning application
defines logical scopes, dependencies, effects and representation meaning;
Orbital enforces those declarations without acquiring database semantics.

The [mining list](../../../orbital/MINING.md#durable-objects-memory-and-local-execution)
preserves the original memory-projection idea. [Ikea's mutation seam](../ikea-composition/semantics-and-integration.md#mutation-local-work-and-publication)
and [Loom's resource questions](../../notebook/loom-objectives-and-architecture.md)
distinguish publication, physical coverage, resident addresses and retained
versions. Their earlier mechanisms and illustrative APIs are not requirements
for this spike.

## The distinction these cases test

A logical object, a conflict scope, a reconstruction unit and a physical page
need not coincide. A transaction can own one row while its selected writer
touches a packed word or page containing other rows. Conversely, one atomic
outcome can cover several objects and representations.

A projection binds addresses to an object, representation, version/transaction
context and access lifetime. A readable mapping establishes local access to
bytes; it does not establish that a logical read is authorized, that its
dependencies are resolved, or that the bytes are committed. The application
binding supplies that coverage before native accesses occur. A page fault
cannot be the sole means of discovering logical reads: after mapping a page,
later loads within it need not fault.

This also separates two permission cases:

- A trusted native kernel can have a bounded access contract narrower than its
  mapped page. Its actual loads and stores, including vector overreads and wide
  stores, still have to fit the supplied physical-access contract. The pointer
  and length alone do not enforce that contract on arbitrary machine code.
- An untrusted process can inspect accessible neighbouring bytes. A declaration
  permitting only one row does not hide other rows on the same mapped page.
  Expose only authorized material, isolate the projection, or use an appropriate
  mediated/instrumented interface. Copying an authorized slice is a valid cost
  to compare; making pages the application's logical conflict scopes is not a
  substitute for authorization.

The useful COW-elision hypothesis is narrower than “there is one writer”: an
exclusively owned resident projection may be modified without saving a fresh
beforeimage when every still-required old or recovery view remains
reconstructible independently of those overwritten bytes. Reader rarity can
guide that physical choice, but cannot change a permitted read's result.

## Strong counterexamples

1. **A late historical reader.** T at position 20 overwrites page P when no old
   reader currently maps it. R later opens a permitted snapshot at position 10.
   Returning P's new contents violates R's view. R must receive a separate old
   projection, possibly reconstructed from retained material. Current reference
   counts alone do not describe all supported future reads.
2. **Disjoint changes lost by page installation or undo.** P initially contains
   `x=0, y=0`. T@10 changes x; U@20 changes y. Installing T's whole-page image
   after U can erase y. Restoring T's whole-page beforeimage on abort can do the
   same. An owner-defined scoped merge or reconstruction can preserve both;
   whole-page replacement is not automatically such a merge.
3. **A mapped neighbour escapes dependency checks.** R first reads x, while an
   earlier pending effect on y shares its page. A mapping prepared just for x
   must not silently permit a later authorized load of incorrect y. The view
   must be valid for the context's permitted observations, or the access
   interface must enforce a narrower boundary. Neither a read floor nor COW
   resolves an earlier pending effect by itself.
4. **One mutator, several byte consumers.** T seals an output, starts hashing,
   persistence or a zero-copy send, then reuses its page. There is only one
   writer, but those consumers can observe changing bytes. Physical ownership
   must include outstanding consumers until they retire; a cancellation request
   does not establish retirement.
5. **Redo without a recoverable base.** The sole backing page is overwritten,
   and retained history says only “increment.” The prior value is gone. A redo
   operation needs its original base and sufficient inputs, decisions and
   executable/interpretation dependencies. A command replayed against current
   state is not reconstruction of its original outcome.
6. **A page latch carries a WAN wait.** T obtains local mapping ownership for P
   and waits for a remote source. U writes an unrelated logical row on P but
   cannot materialize it while T holds the latch. This creates interference
   absent from the logical protocol; a pager that needs the same held resource
   can create a resource cycle. Release physical bookkeeping ownership before
   a logical or WAN wait. Preserve needed bytes through leases, separate views
   or reconstruction instead of turning that ownership into a transaction lock.

## Twelve worked application histories

These are analytical scenarios unless the evidence section below names a
specific implemented fragment. Positions denote logical transaction order,
not the time at which physical pages arrive.

| Case | History and required observation | What it distinguishes |
| --- | --- | --- |
| **1. Two OLTP rows on one page** | T@10 sets x to 1; U@20 sets y to 2 and installs first. T then commits or aborts. U's y remains 2 in either case; x follows T's outcome. | Logical independence versus whole-page overwrite/undo. Private views or scoped effects must preserve another transaction's result. |
| **2. Analytical scan with a late fault** | R@10 reads the first page and pauses. T@20 rewrites another page. R touches that second page for the first time after T publishes. R still sees its position-10 values. | Eager beforeimages versus old-view reconstruction on demand. Include reconstruction latency and retained dependencies, not just copied bytes. |
| **3. Bulk RMW followed by a blind replacement** | T@10 increments a collection slowly. U@20 completely replaces one row and commits before T. An eligible later reader may use U there; R@15 still needs T's earlier result. | Physical arrival order versus logical versions; complete supersession versus ordinary RMW. A later replacement does not erase obligations to older views. |
| **4. CDC append and source offset** | A batch appends records to fresh pages and advances its source offset. Crash after writing the pages but before deciding. On recovery, records and offset follow one outcome; tentative bytes alone establish neither. | Fresh unpublished allocations need no old image, but still need atomic publication and recovery. Corrections to existing records are not fresh-page appends. |
| **5. MERGE into packed rows and indexes** | T changes one indexed row; its encoder issues a wide store containing neighbouring unchanged fields. U concurrently changes one of those neighbours. Required index/summary state must match each visible outcome. | Physical issued-byte coverage versus logical effects. Reporting only changed bytes cannot justify overwriting a neighbour or publishing stale derived state. |
| **6. Refresh with a live destination correction** | Build a new generation while another transaction corrects a destination row. An old reader retains the previous root. Activation must retain the old view and either preserve the correction or explicitly use a historical/source-authoritative replacement contract. | Private construction and small root publication versus live reconciliation. Pointer swaps and COW elision cannot choose the application's semantics. |
| **7. Dictionary-backed conversion** | Convert representation R1 using dictionary D1 into R2 using D2. A reader still holds an R1 view. Publishing R2 cannot reinterpret that pointer or retire D1 while the old view/replay recipe requires it. | Logical equivalence versus representation and dependency lifetime. Conversion should read a stable source version, not a concurrently changing working buffer. |
| **8. Durable document editor** | A writer grows a rope or vector while an old revision remains open. Growth within reserved address space may preserve the writer's pointers. Relocation or shrink must not silently redirect pointers retained by either view. | Stable virtual addresses within a lease versus stable object identity across revisions. Address reuse also needs an incarnation/lifetime boundary. |
| **9. Workflow queue and external intent** | Claim a job, build a result and create an external-send intent in one transaction. Crash after tentative memory changes. Recover the transaction outcome; replaying its memory updates must not send the external action again. | Ordinary memory manipulation versus transaction authority and external delivery. Abort discards tentative work; uncertainty does not license a fabricated abort. |
| **10. Training while serving inference** | One writer updates model weights while inference retains the old model. A late inference request is also allowed to select that old version. Both must obtain the original weights even if there is only one training writer. | Single-writer status versus incompatible reader views. Compare changed-page copies with retained checkpoint/operation reconstruction and its amplification. |
| **11. Durable simulation grid** | Workers update disjoint cells sharing pages while a renderer reads a snapshot. Neighbour-dependent steps use the declared logical dependencies; a complete tile replacement can have different semantics. | Application operation laws and snapshot visibility versus physical sharing. Shared pages do not supply commutativity or justify page-wide logical exclusion. |
| **12. Persistent hash table under pressure** | Resize discovers cold buckets. A worker faults while holding an allocator mutex and the pool is full. Cancellation follows, then a late completion targets the old mapping. | Independently runnable fault service, reconstruction capacity and generation-safe completion. Another thread alone does not solve full memory or a mutex needed by its own pager. |

## What the initial executable evidence establishes

[probe.py](probe.py) and [its retained output](evidence/probe.json) are finite,
deterministic models with logical counts, not machine timings:

- **24 two-writer histories** combine six legal write/resolve event orders with
  four commit/abort combinations. They expose unsafe whole-page installation
  for two disjoint fields against a serial reference over resolved scoped
  effects. That reference is not a separately tested replay implementation.
  Separate assertions illustrate a late old reader and unsafe
  whole-page rollback. They do not implement ordinary pointers, pending-read
  resolution or a distributed transaction protocol.
- **90 cache-trace cases** compare full eager pinning, demand loading, guessed
  windows and known bounded access regions at three capacities. One serial
  reader, LRU eviction and synchronous loads are assumed. “Known” gets an
  owner-supplied access description and is not offered for pointer-chase cases;
  these counts establish no latency hiding or scheduler advantage.
- **48 elision counter cases** charge old-view reconstruction to specified
  reads and aborts over several replay depths. Retained bases and replay inputs
  are assumed available. The model estimates no reader probability and models
  no real sharing, durability, reconstruction cost or retention horizon.
- Eight exhaustive small resource models distinguish exhausted workers from
  exhausted memory and a lock cycle involving the faulting caller. Further
  examples show retained chunks preventing an acyclic pipeline from finishing, and
  preserve an outstanding backend buffer after cancellation. A separate layout
  counter illustrates false sharing; it is not a CPU coherence measurement.

[capabilities.cpp](capabilities.cpp) tests read-only page protection and probes
API availability, with [a recorded local result](evidence/capabilities.json).
It is not a UFFD pager, object runtime, secure sandbox or performance test.
Advertised features and accepted setup calls do not establish the complete
mapping, isolation or fault-recovery path.

The separate [views.cpp](views.cpp) executes four native one-page cases:
UFFD missing and minor resolution, write-protect COW preserving existing and
late old readers, and exclusive reuse with an independently reconstructed old
view followed by abort. [The local output](evidence/views.jsonl) records all
four passing. Reconstruction uses a toy in-memory base/operation, not a durable
journal; there is no mixed-load, crash-recovery or concurrency-performance claim.

## Focused comparisons worth making

1. **Version oracle before memory optimization.** Extend the small history
   comparison with actual reads at fixed positions, earlier pending effects,
   crashes, late installation and no-effect outcomes. Compare always-private
   projections, exclusive frame reuse with reconstruction, and deliberately
   unsafe page install/undo. Check every observation and committed outcome,
   not only final bytes.
2. **Copy versus reconstruction on the local machine.** Compare eager copies,
   demand COW and eligible frame reuse under matched dirty fractions, reader
   arrival patterns and replay depths. Charge faults, CPU, copied/reconstructed
   bytes, retained history, refusals and unfinished work alongside tail latency.
   Include one-page requests whose operation replay needs a much larger object.
3. **Faults under finite resources.** Exhaust ordinary workers, page buffers,
   reconstruction space and completion capacity separately. Inject faults while
   application locks are held, plus cancellation and delayed completions. Check
   that decision, pager and recovery work can still execute without waiting on
   the work they must unblock.
4. **Conversion and mapping lifetime.** Hold views through dictionary conversion,
   growth, shrink, relocation, eviction and address reuse. Check old-view
   contents, live pointer guarantees and rejection of stale completions. Compare
   page-separated allocations with packed layouts without changing their
   logical conflict semantics.

Linux's [userfaultfd documentation](https://docs.kernel.org/admin-guide/mm/userfaultfd.html)
describes page-fault delivery, write protection and mapping-change events. These
are physical mechanisms; their availability does not provide object semantics
or observe every access after a page becomes readable/writable. Fault resolution
must synchronize with mapping changes.

The [mremap documentation](https://man7.org/linux/man-pages/man2/mremap.2.html)
explicitly warns that relocating a mapping invalidates absolute pointers into
its old location. Leaving the old range mapped with `MREMAP_DONTUNMAP` causes
subsequent accesses there to fault; it does not itself preserve an old object
version. A durable heap can choose fixed-address or relocatable representations,
but the application must supply that interpretation rather than Orbital
guessing which bytes are pointers.
