# Borrowed and slot-backed wide values

The borrowed-inline read API is [promoted into Ikea](GRADUATION.md); slot-ID-backed values
remain research. Compare the complete-value getter with a borrowed inline address
and four/eight-byte slot-ID maps backed by a contiguous reusable value store.
The maintained layout and existing getter are unchanged.

The [findings](RESULTS.md) distinguish the address-return benefit from deferred
payload consumption, and test index capacity independently of payload capacity.
The [complete scorecard](evidence/zen5/tables.md) includes rejected treatments,
actual peer controls, writes, growth, backing memory and the small-value control.

The question is whether actual payload-consuming reads and writes justify a
different wide-value interface or backing. Returning an address alone is not a
completed read. The benchmark digests every eight-byte limb, and separately the
first sixteen bytes of values wider than sixteen. It checks hits, misses, random
50/50, alternating 50/50 and runs of 64 hits/misses. Automatic selection surrounds
the complete lookup and payload consumer; explicit predictable/unpredictable
loops expose its choices and costs.

## Implementations

- `materialized`: maintained Ikea `get` and `modify`.
- `borrow`: thin adapter over the maintained `get_borrowed` API; its original
  implementation survives in the captured spike sources. A miss supplies
  readable initialized zeros. The research-only exclusive increment casts away
  constness and changes one limb in place;
  this does not claim the exception guarantee of the full-value `modify` API.
- `slots` / `slots32`: an eight/four-byte Ikea map plus reusable integer IDs into aligned
  contiguous value storage. Slot zero is the readable miss row. Fresh insertion
  uses Ikea's prepared private write, avoiding a second key lookup. Erase currently
  retrieves the ID and then invokes ordinary erase, costing two index searches.
  External-ID peer adapters use `find` then `emplace` for a fresh key; those
  write rows measure these prototype helpers, not each library's best possible
  custom ID-store integration. Their read paths use the native lookup. Inline
  peer writes use native `insert_or_assign` / direct existing-value assignment.
- Actual pinned Abseil, Boost 1.92 and Folly's F14FastMap/F14ValueMap. Abseil,
  Boost and F14Fast also use exactly the candidate's slot store as indirection controls.
  F14FastMap chooses vector storage for larger entries itself.

For values at most eight bytes, the borrowed candidate retains the maintained
getter. It does not replace the established small-value path with a pointer
return. The additional W32/b11, 13,926-entry fixture repeats the earlier
small-value load point alongside the 85%-occupancy wide-value comparison.

The store reserves the expected final population, grows geometrically if that
is exceeded, and retains holes in a measured free-ID vector. A store expansion
can move payloads. Index growth alone leaves it untouched; stable pointers across
arbitrary mutations are not promised. Values and IDs would need coherent owner
retention/version selection in a real consumer; this spike compares private
owners with no concurrent mutation or MVCC overhead on any arm.

## Comparisons and boundaries

The native runner uses Zen 5, Clang 21.1.8, AVX-512, one pinned measured CPU,
sequential counterbalanced process arms and repeated input seeds. Warm traces
are executed before timing. Cold intervals follow CLFLUSHOPT over all owned
table/store backing and SFENCE. Query/input vectors remain outside that flush.
Lookups in the interval can warm or prefetch later accesses: the label describes
conditioning, not a claim that every load reaches DRAM. Plain and 32-request
home-prefetch loops are separate observations. A second cold treatment keeps
sixteen pending lookups: it prefetches each resolved payload, interleaves other
lookups, then digests the actual payload. A third uses an actual native Loom
Ring with sixteen frames, yielding after address resolution and payload
prefetch. Both cover the full consumed window, including an unaligned last
cache line. Those original treatments stage only payload consumption: the slot
adapter loads the ID synchronously. The fair slot follow-up uses
`match -> prefetch ID cell -> yield -> load ID, prefetch payload -> yield -> consume`
in both the manual pipeline and actual Loom Ring. It retains sixteen outstanding
queries and applies the same stages to Abseil, Boost and F14 external-ID maps.
`locate_id` borrows the ID-cell address without reading its contents; `resolve_id`
loads it only in the next stage. Admission, yields, physical drain and reduction
are timed; worker,
arena and bank setup/teardown are outside the interval. The automatic selector
admits/drains each 256-query batch; this charges that composition, and does not
qualify a controller that preserves frames across selections. All peers get
both deferred-consumption treatments. The materialized result must copy the consumed bytes into a live pending record;
borrowing a pointer to its temporary would be invalid. Boost has no equivalent public
home-prefetch operation; its own lookup implementation is used unmodified.

Writes include full replacement, one-limb increment, new insertion, mixed
upsert, erase and refill. Caller-prepared capacity keeps new insertion separate
from growth. Every written state is checked for membership and every payload
byte. Same-seed Ikea growth constructs and retains a separate source image;
peer native rehash can release the old array. Growth timing preserves
that difference rather than treating the operations as identical ownership APIs.

`bytes` counts allocated table/value/free-ID backing, including capacity. The
qualified scorecard uses steady backing. Captured `peak_bytes` missed temporary
old/new overlap in Ikea base-buffer growth; those raw counters are excluded from
published comparisons. The current adapter fixes and explicitly checks that
accounting, but its correction is not backdated into the captured measurements.
Backing counts exclude owner descriptors, allocator metadata, fixture vectors, validation maps and accounting instrumentation.
The same counting allocator covers peer backing. Store slack and holes are
included. This is not RSS or a prediction of MVCC retained-version memory.

The V64 capacity ablation keeps populations fixed at 6,963 hot / 2,673,868 cold
entries. Only the slot index doubles its bucket count: hot index occupancy falls
from 85% to 42.5%; cold falls to 31.875% because its bucket capacity also grows
from 24 to 32. Payload reservation stays fixed. This tests the reason to decouple
index and value density, rather than forcing the ID index to use the wide inline
table's occupancy policy.

Values of 8, 16, 32, 64 and 128 bytes exercise the small-value control and wide
payloads. Non-8/64 widths initially reuse the maintained 64-byte encoding profile;
no newly measured encoding choice is claimed. This ordinary-table comparison
does not establish the best backing for Engine's directory. Directory geometry
and consumer verification belong to Engine and are outside this promotion.

## Run

```sh
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/hashtable-wide-values/native-loom.sh --machine zen5 \
  --instance-type c8a.xlarge --capacity on-demand --env WIDE_STAGED=1

orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/hashtable-wide-values/native.sh --machine zen5 \
  --instance-type c8a.xlarge --capacity on-demand \
  --env 'WIDE_VALUES=8;16;32;64;128' --env WIDE_MODE=full
```

Abseil uses the shared verified 20260817.0 provider at commit
`2065f4ded0558c6f89fee67c8e5228feb4eb960e`. Boost is pinned to 1.92.0.
The CMake provider builds upstream F14's support sources against its generated
configuration header, with SIMD/CRC flags matching the benchmark. It uses the
unchanged Folly `v2026.10.19.00` commit
`ea41cb2f0b3c13a225a616ffef33cdfa85eb4aad`, verified archive SHA-256 in CMake.
It supplies no symbol stubs or replacement algorithms. Native output retains
the generated configuration, binaries, disassembly, exact process commands,
checksums, repetitions and host/compiler/package receipts.

`native-loom.sh` runs the paired full-window direct/manual/Loom cold panel and
native ASan/UBSan checks. Add `--env WIDE_STAGED=1` to the worker command for the
proper ID-before-payload panel, including the less occupied V64 ID index. It
reruns direct and payload-only consumers in the same captured build. The staged
checks cover P/U/Auto and partial lengths 1, 7, 17 and 4,111 on actual x86 Loom. `native-density.sh` runs the fixed-population capacity
ablation with all controls. Submit either through the same worker command.
`resume.sh` / `resume.py` are the exact recovery recipe for the interrupted main
run: verify completed CSVs and captured binaries, finish only missing cases, then
run the newer paired panel. The artifact references preserve both host identities.

`collect.py RECOVERED DENSITY OUTPUT --staged STAGED` verifies each original process receipt and
builds compact per-repetition CSVs. `scorecard.py OUTPUT` regenerates the report
without native execution. The evidence recipe uses these retained CSVs; original
source captures, binaries, assembly, complete command lines and omitted pilot
variants remain recoverable from the linked content-addressed archives.

The raw peer executable's `b` column is its unused compile-time placeholder (9).
`fixture_b` in the retained CSVs records the requested comparison geometry;
comparisons match W, value width, population and temperature. It does not
pretend that a peer map has Ikea buckets. The final report groups on that
explicit fixture metadata rather than the peer executable's placeholder.
