# Orbital design workspace

Ashton's thinking space. This outline records his areas of responsibility;
the relationships are deliberately left open. Agents should contribute here
only when Ashton explicitly asks.

Orbital has five organising concepts/subnmodules.

## Object

Heirarchy: Object -> Representation -> Part -> View

Objects belong to a shard. They hold a logical state, lock set, and version history; version IDs are taken from journal epochs. Objects can be realised by representations.

Representations belong to a replica. They hold a physical state. They consist of multiple parts.

Parts belong to a representation. They hold a location and byte sequence, accessible via a port.

Views belong to parts. They hold authority over read and write protection. They map a part's byte sequence to an allocation, in whole or part.

Orbital owns and publicly exposes all hierarchy members.

To create, update or delete an object:

1. Write an event to the producer journal describing the intended mutation. Send persistence and admission requests through ports and await. Begin the consumer journal epoch holding that event.
2. Assuming no pre-existing object, create the object and replica-local representation together in one operation. Create the first part, initialising location and byte sequence to null. Create the view and allocation together, with writes allowed, and growing the allocation as required.
3. Write using ordinary memory operations. Record damaged byte ranges and stage them, spreading port utilisation over an epoch's lifetime. Bump the consumer journal version.

```
ROUGH NOTES ON VIEWS (needs spike)

Lock sets project to view masks. Ikea typically supplies the projection function and domain/image interpretation and application. whatever schema lock sets abide to, i guess access control lists will use something similar.

views might not have active readers/writers, they could simply be cached (is this a view? or a part accessed via a memory port? does it matter? if there are tradeoffs, i guess they're around reclaim/reference-counting and the liklihood a part will be re-accessed in the near future. which TUs are best-positioned to own these concerns?)

views and parts should identify what version range they are valid for; the upper bound can take one of two sentinel values: "latest" and "draft". only draft views are mutable; for a given history, exactly one view has the latest XOR draft tag; you create draft from latest when starting mutation, and create latest from draft when finishing mutation. idk how to approach versioning for representations, but the approach should be simple.

views need protection when shared with untrusted workers. hardware enforcement is viable when required protection is uniform across a page (incl. for Firecracker? can you prevent such extensions changing hardware protection?). when an untrusted worker is only allowed to see part of a page it should be masked when it enters the untrusted context; if only allowed to write to part of a page that should be masked when it exits the untrusted context. trusted contexts don't require hardware protection and masking; or at least, paying for such protection isn't forced.

someone might not need whole part in view (just a few pages); i think here you want an allocation that has space for the entire part, and then the savings from port utilisation rather than reducing memory pressure.

when someone wants to open a view, there's a few ways to do it: from part, or copy/take from an existing view. deciding what method to use is an optimisation question: method implementation and determining whether a method is allowed/safe belongs with Orbital, while deciding which of the allowed methods to use is probably best-located with Loom. so, if you want a view version not already held by a view you obviously have to get from a part (or, like, another VM's cache... hmm, sometimes reading memory of VM your placement group will be faster than reading your own disk?; or, worse, replaying journal from an old snapshot). unless a view is a draft, it's essentially always safe to copy. what kind of "taking" is possible, safe, and desirable? and do we take entire views, or individual pages? taking a "latest" view (and its underlying allocation) with no active readers/writers into a draft and modifying in-place is safe if the latest view version is recoverable, but might not be desirable if recovery is expensive and we expect a reader will need access before the draft can publish; i guess you could create a new allocation for the part on the first write, and copy the latest page version to that new allocation (COW), and then if a reader needs access it could take that new allocation and copy on read; also safe to take non-draft into an untrusted context with read-only permission (no conversion to draft; but changing hardware protection and/or masking is a modification which might consitute a "take"). taking a draft needs more thought (draft -> implies active writers; multiple workers can write to draft concurrently, it's not Orbital's responsibility to ensure safety here). when a view has active readers i guess you would make a new allocation, but not necesarily copy immediately, but just copy on both read and write touch (similar to COW described earlier, but the other way around; maybe this is another kind of taking/borrowing/forking). in some cases the UFFD/TLB overhead might overshadow the cost of just copying in the first place; i think Loom should decide what to do here (most/all consumers — Ikea, Engine and Shore — that want access will get their views/allocations via Loom rather than Orbital directly).
```

I think seeing state machine and sequence diagrams for all the core operations and types would bring clarity here. Like, idk, covering create/read/update/delete under various circumstances; core operations should be (mostly) simple operations.

Bootstrap regions: distributing to allocations.

Create/read/update/delete

They can be created/read/updated/deleted.

Lock

Operations:

- Create/update/delete object; read object gives its representation

## Allocation

Simple alloc/free/grow/etc interface. Doesn't understand ownership. Non-Orbital modules may use directly, but more-often access through objects.

Heirarchy: Arena -> Group -> Allocation

Group has metadata region that might mix trampolines, occupancy information, hints for recently freed allocations describing likely L2/LLC-hot (Loom wants this), layout of metadata should optimise access. i think there was a spike that said 1µs for allocations, which feels slow if we already have the physical memory mapped. i wonder if speculatively mapping pages ahead of time might bring this down to something like 100-200ns. i think most objects will map to "virtual" sized allocations; i mean, i don't think all the version control machinery would be economical for 16 B allocations (such allocations are probably only useful for temporary scratch, bookkeeping local state, and that kinda thing)

## Journal

- Shard
- Log stream

- Witness, Producer, Consumer, Processor, etc.

## Port

- Disk
- Network

```
Will probably compress data that goes through ports. if not something like zstd, at least densely packing used bytes. like for the 256kb(?) bootstrap region, we'll probably expand this into quite a few allocations.
```

## Worker

- SixDB thread
- WASM extension
- Firecracker extension

## First viable module

By targetting an embedded profile the journal, port and worker submodules can be kept initially thin. The key is that these thin implementations must be substitutable.
