# Looking ahead: requirements the planned submodules put on hot paths

These are leads drawn from the current code, the measured costs in
[README.md](README.md) and the earlier research in the
[Orbital reference](../../notebook/orbital/reference/README.md),
[views findings](../orbital-views/FINDINGS.md) and
[Stage 2 outcomes](../orbital-mvcc/OUTCOMES.md). They are not designs.
Ashton's [workspace](../../notebook/orbital/ASHTON-SCRATCH.md) owns the
relationships between the submodules. Loom sits above them: it arranges
execution, placement and scheduling, and short operations call the submodules
directly.

## Cross-cutting: records need cheaper lifetimes than one Allocation each

Almost every candidate record in [the design headers](../../design/orbital/include/orbital/candidate)
(Event, Append, Batch, AdmissionRequest, Request, Execution, Material, Lock,
Buffer, RetainedView) carries an `allocation::Hold storage` plus an atomic
reference count. At today's costs, one finite allocate/release plus one Hold
cycle is about 80 ns on Zen 5 and 150 ns on Graviton before the record does
anything (see README). A point write that creates four or five such records spends
around half a microsecond on bookkeeping. That is affordable for the 500 µs
durable p99.9 target, but not for the sub-microsecond loss-tolerant path or
for epoch batches of thousands of events.

Three shapes cover most of these records:

- **Batch-lifetime records.** Events of one admitted batch, its frontiers
  and its acceptance list are created together and retired together. A
  per-batch Window with bump placement retires them with one release and needs
  no per-record identity. This matches Recommendation 1 of the views findings
  (batch capture and publication).
- **Owner-local records.** A record only its Lane touches can use a plain
  count (`Ref` already takes type-specific hooks) instead of an atomic RMW.
- **Escaping records.** Requests and RetainedViews that outlive a Task need a
  real Allocation and a foreign-release route. That route currently costs much
  more than local release (see `foreign/release` in the README), so it should
  be the exception.

## Allocation

- The warm reuse path costs 12–13× a glibc malloc/free pair on these hosts,
  and more against mimalloc-class allocators (3–4 ns in the descriptor study).
  Most of it is validation and re-derivation inside the owner transaction, not
  the geometry: see the `lean` and `bound` variants. A per-class bound-slot
  cache reaches the speed of a classic thread cache, but it changes observable
  reuse and accounting. Statistics, prepared reuse and cross-class reuse would
  each need to account for cached slots.
- Ashton's scratch note asks whether pre-mapping could bring ~1 µs
  allocations down to 100–200 ns. The ~1 µs is the cold path: one `mprotect`
  per page plus one first-touch fault per page (`alloc/fill/1024`: 460 ns on
  Zen 5, 730 ns on Graviton). Mapping a contiguous run with one `mprotect` and
  prefaulting it with `MADV_POPULATE_WRITE` (`premap`) halves it, to 235 and
  420 ns. The run must stay inside the class's own pages, or small classes lose. The policy question
  is who pays: foreground batching, or Preparation on a service Lane, which
  already exists for exactly this.
- Every local final release pulses `Pool::capacity_changed`. That is cheap
  with no waiters. With N Tasks parked on capacity, every free wakes all N, and
  each retries and re-parks: O(N) per free under pressure. Capacity waiters
  want either FIFO hand-off of one waiter per freed class or a coalesced
  notification at the next poll boundary.
- Assertions stay on in optimized builds. Several hot-path assertions were
  `on_lane` calls through a TLS wrapper that registers a destructor (~5 per
  allocate/release pair). The `lean` variant makes the owner token trivially
  constructible and inlines the check, so keeping assertions costs about a load
  and a compare.

## Object

- Lookup is `hash → bucket (handle) → pages_[h/32] → Page → Entry`, with an
  Entry dereference for every probed bucket. At 1M Objects a dependent bind
  costs 333 ns (Zen 5) and 433 ns (Graviton). The `tagged` variant stores 32
  more hash bits per bucket and compares Entries only on a tag match: misses
  run 1.4–2.1× faster and hits about 1.1–1.2× faster. Entries reachable
  without the page indirection would remove one more miss per hit.
- The hash folds `high ^ low·φ` before mixing. An ID family whose words are
  correlated through that same multiplier collapses: `{i·φ + 1, i}` made 1,024
  objects cost ~240 ns per bind and 65,536 cost ~30 µs (OrbStack, direction
  only).
  Ordinary sequential or random IDs are fine. Mixing each word independently
  before combining removes the hazard for a few cycles.
- Publication: each change pays capture, a PMR allocation, duplicate
  preflight and directory work: 100–600 ns per change with the test material,
  growing with catalog size. Ikea's real containers
  and Engine's navigation will decide the real costs (OUTCOMES lessons 4–5).
- When concurrency arrives, `Scope` is a doubly linked list on the Core, and
  `floor()` reads `first_scope`. That is fine for one owner. Readers on other
  Lanes need per-Lane reader slots or an epoch-style registration, so that
  enrolment does not write a shared cache line. Views Recommendation 4 (derive
  the cut on demand) already prices this.

## Journal

- Admission fans many producers into one leader or consumer Lane. Today's
  Lane is an intrusive MPSC queue with one `exchange` per post on a shared
  line. The consumer handles 23–32 ns per message on Zen 5 and 40–90 ns on
  Graviton. Publishing a pre-linked chain with one exchange (`chain` variant,
  `post_chain`) brings that to 5.5–7.5 ns. Producers already batch events per epoch,
  so the API fits naturally.
- Local cut derivation and producer frontiers: views Recommendation 4
  measured 23 ns versus 3.6 ns per publication for shared versus per-producer
  progress. Keep per-producer frontiers, and have readers derive the cut.
- Fill wait: batching must not delay an idle point operation (DESIGN step 1,
  views Recommendation 1). That calls for a Loom policy that flushes on idle
  poll rather than on batch size alone.

## Port

- Completions arrive on Lanes as Messages. With io_uring, one CQ drain can
  produce dozens of completions for the same Lane: that is `post_chain` again.
  The backend can link them and publish once.
- Registered buffers need stable addresses through submission and completion.
  Window Holds currently add about 24–30 ns per retention cycle for reader-bin
  tracking (live-tail study). A Request that retains a whole registered
  buffer pool needs one Hold per buffer, not one per I/O.
- `mprotect` and `MADV_DONTNEED` on a helper still disturb other workers in
  the address space through TLB shootdowns (views TLB study: 64-page batching
  7.76 µs versus 355 µs page-by-page). Port buffer pools should be mapped once
  and recycled, not trimmed between I/Os.

## Worker (Orbital threads, Lanes) and Loom above it

- `Worker::poll` calls `pending()` after every Task step. `pending()` reads the
  producer-side line that foreign posters write. Under foreign load, that is
  likely a cross-core miss per step (inferred, not measured here). Checking `consumer->next` first, and the producer
  pointer only when that is empty, keeps the common case owner-local.
- `detach` of a queued Task walks the queue, and `unlink_wait` walks the
  Signal's waiter list: both O(n). That is fine at today's Task counts. A
  doubly linked intrusive list makes both O(1) if Tasks become numerous, as
  with many waiting reads during I/O.
- Ring dispatch is already lean (`preserve_none`, `musttail`, 64 B Slots). The
  earlier Loom studies own its tuning. Nothing here suggests revisiting it.
- WASM and Firecracker executors (Worker submodule) will cross isolation
  boundaries per call. They want batched invocation and pre-registered
  Projections (candidate `executor.h` already reserves descriptors). Per-call
  costs belong in the same harness once an executor exists.
