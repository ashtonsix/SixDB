# A ready point read

Read one value from already available memory. This example uses one byte per
key so that the Orbital path is visible; Engine/Ikea supplies the real lookup
and encoding. The C++ blocks form one translation unit, in reading order.

The caller supplies one Loom Worker, stable BufferCache and enrolled reader,
like the state behind a database handle. The cache uses that Worker's Lane;
Loom chooses policy and the Worker owns its allocation Pool. Reserve metadata once:

```cpp
#include <orbital/candidate/journal.h>
#include <loom/worker.h>
#include <array>
#include <utility>

namespace orbital::candidate::examples {

Result<void> setup_caller(loom::Worker& worker, object::BufferCache& cache,
    journal::Consumer& consumer,
    journal::Reader& reader, Ref<allocation::Arena> arena,
    std::size_t cache_capacity, std::uint64_t task_capacity, int cpu = -1) {
  auto ready = orbital::worker::adopt(worker.lane, cpu);
  if (!ready) return ready;
  cache.lane = &worker.lane;
  ready = object::reserve(cache, cache_capacity);
  if (!ready) return ready;
  ready = loom::configure(worker, std::move(arena), task_capacity);
  if (!ready) return ready;
  return journal::enroll(consumer, worker.lane, reader);
}
```

The reader record comes from the handle's independent storage owner. A failed
adoption leaves the Lane unadopted. Failed cache reservation or configuration
leaves no live Pool; relinquish the adopted lane. Failed enrollment leaves the fresh Reader
unenrolled but the Worker configured: finish Loom shutdown before relinquishing the Lane.
At shutdown, close native contexts and leave the reader before its
notification storage retires. Drain cached backing, preparations, Views and typed
Ring banks before actual Loom shutdown; the Worker does not own or clear this
cache. Then relinquish the Lane. Persistent material may require a
stable Worker handed to another thread rather than destroyed with this caller.

A point call uses that prepared owner without a Task, admission or lane gate:

```cpp
Result<std::byte> point(loom::Worker& worker, object::BufferCache& cache,
    journal::Consumer& consumer,
    journal::Reader& reader, ReplicaId replica_id,
    ObjectId id, PartId part_id, std::uint64_t key) {
  auto& lane = worker.lane;
  if (reader.lane != &lane || cache.lane != &lane)
    return std::unexpected(Error::invalid_argument);
```

Acquire the latest published State, then find the object, this replica's
representation and the requested part. The native context protects its state
through the enrolled reader slot; it does not retain a shared State reference
for each call. Hierarchy pointers borrow that protection. The consumer's
publication lane need not be the caller's lane.

```cpp
  auto context = journal::read(consumer, reader, {});
  if (!context) return std::unexpected(context.error());
  auto selected_object = object::select(*context, id);
  if (!selected_object) return std::unexpected(selected_object.error());
  auto representation = object::representation(**selected_object, replica_id);
  if (!representation) return std::unexpected(representation.error());
  auto part = object::part(**representation, part_id);
  if (!part) return std::unexpected(part.error());
```

Open a view of exactly the byte needed. The view protects the actual memory
and exposes its byte bounds and padding. Its borrowed range array remains alive until release.
False means the ready path cannot supply those bytes; it starts no cold work.

```cpp
  if (key >= (*part)->size) return std::unexpected(Error::absent);
  const std::array ranges{ByteRange{key, 1}};
  object::View view;
  auto ready = object::try_view(*context, {*selected_object, *representation, *part},
      cache, {ranges, {}}, view);
  if (!ready) return std::unexpected(ready.error());
  if (!*ready) return std::unexpected(Error::unavailable);
```

Read and copy the value before releasing the view. The release hint says the
memory was just accessed. The view's destructor also closes it on an early
return, while its range array and selected state are still alive.

```cpp
  auto bytes = object::read(view, ranges.front());
  auto result = bytes ? Result<std::byte>{bytes->front()} :
      Result<std::byte>{std::unexpected(bytes.error())};
  object::release(view, {.temperature = allocation::Temperature::hot});
  return result;
}

} // namespace orbital::candidate::examples
```

A query can keep one context and a larger view for many native lookups. Access
within that qualified region needs no per-point permission or retain.
This standalone example includes selection, qualification, View registration and
release; compilation says nothing about their latency. A caller that wants to
wait for missing bytes can use the [cold read](jobs.md). A caller whose use must
outlive native scope can [retain the View](extensions.md).
[Native batches](native.md) show where optional memory rotation fits beneath a
larger operation without adding work to this point path.
