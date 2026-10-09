# Stage1 ownership behind shared-shape collections

[allocation_profiles.cpp](allocation_profiles.cpp) measures the real Stage1
allocator behind 64 encoded Objects. It uses frozen `7f73c81` Orbital allocation
and current shared-shape ownership. Linux AArch64, Clang 21.1.8, libstdc++ 15,
glibc 2.42, 4 KiB pages. This is state accounting, not a timing comparison.

The small physical owner token does not remove its underlying allocator state.
These collections retain **31,376–35,152 B** of Stage1 metadata, plus their Lane,
shared Shape/realizations, catalog, and OS thread state. Keep this shared cost
separate from the marginal cost of another Object in an existing Pool.

## Real bodies and owners

The actual versioned consumer constructs and independently reads every row.
TuplePack puts four fields in one 23-byte row. The four headless Local
SeriesPack columns use 64/64/20/32-bit fields and eight-row tiles.

| Records/Object | Tuple body | Four column bodies | Aligned grouped column extent |
| ---: | ---: | ---: | ---: |
| 3 | 69 B | 64 + 64 + 20 + 32 = 180 B | 224 B |
| 64 | 1,472 B | 512 + 512 + 160 + 256 = 1,440 B | 1,472 B |

Independent placement allocates each Part separately. Grouped placement puts
one Object's Parts in one allocation, aligning each column to 64 bytes. This
is separate from Stage1's physical Group, which has 256 reusable identities.
The one-Part profiles have identical independent/grouped placement.

Capture establishes a genuine full-range Hold before it releases the original
Allocation. The realization stores its 8-byte Storage token; physical uses
share the allocator's preallocated Storage record. There is no additional
per-Hold heap header. The probe checks one physical user per independent Part
or per grouped Object, and verifies the full held extent.

Schema, source Shape/descriptors, original Allocation handles, writable aliases
and the decoded oracle end before the collection and escape observations. The
shared proven Shape owns the surviving geometry/read controls. The diagnostic
retains a scalar observation and 64 realization pointers; it does not duplicate
a Store or pretend those pointers implement catalog ownership.

## Allocator state at 64 Objects

Empty Arena/Pool setup requests **752 B in seven C++ heap blocks**:
Arena 32, ArenaState 208, thread startup control 24, thread callable 48,
Pool 64, PoolState 368 and Pool registry capacity 8. The startup control and
callable remain until the reclaimer exits; the earlier type census's 680 B
setup omitted their 72 B. This setup's observed glibc usable extent is 808 B.

The adopted Lane is another **256 B** caller-resident record. The reclaimer has
an **8 MiB stack reservation** and **4 KiB guard** on this machine. These are VA,
not committed bytes. TLS, kernel thread state and libc allocator chunk headers
are additional; neither requested heap bytes nor usable extent is process RSS.

| Profile | Additional inventory heap | Control mappings | Stage1 metadata total | Mapped bodies | Shape + 64 realizations |
| --- | ---: | ---: | ---: | ---: | ---: |
| Tuple, 3 rows | 22,432 B | 8,192 B | 31,376 B | 8,192 B | 2,787 B |
| Columns, 3 rows, independent | 26,208 B | 8,192 B | 35,152 B | 65,536 B | 5,816 B |
| Columns, 3 rows, grouped | 26,208 B | 8,192 B | 35,152 B | 16,384 B | 4,280 B |
| Tuple, 64 rows | 26,208 B | 8,192 B | 35,152 B | 98,304 B | 2,787 B |
| Columns, 64 rows, independent | 26,208 B | 8,192 B | 35,152 B | 131,072 B | 5,816 B |
| Columns, 64 rows, grouped | 26,208 B | 8,192 B | 35,152 B | 98,304 B | 4,280 B |

Each profile uses one physical Group. Inventory includes its full fixed record,
FiniteGroup, actual vector capacities and the Pool's 8-byte Group pointer
capacity. The Group's 256 Storage slots and two 256-entry hint arrays are
included; counting active Storage records again would double-charge them.
The first row uses palette A; the others use palette B. Their body/binding VA
reservations are 2,609,152 / 4,493,312 B respectively. Their 4,096 B binding
plane also occurs within the 8,192 B control-mapping observation: these VA
observations overlap and must not be added as independent reservations.
Actual mapped body bytes exclude the binding/control plane.

The allocator's charged metadata counter is 30,616 / 34,392 B. The total above
also includes its uncharged Arena/Pool setup and Group pointer capacity.
It excludes the separate Shape/realization resource, Lane and OS thread costs.
Shared Shape is 227 B for Tuple or 184 B for columns. Each independent
realization requests 40 / 88 B for one / four Parts; grouped requests 40 / 64 B.

## One Part escapes

The probe retains the first Object's value Part, releases all 64 realizations
and the collection's Shape reference, then reads the first and last value using
the retained native controls. Exactly one Storage record/user survives.

| Profile | Selected encoded extent | Full held allocation extent | Physical home capacity |
| --- | ---: | ---: | ---: |
| Tuple, 3 rows | 69 B | 69 B | 96 B |
| Columns, 3 rows, independent | 20 B | 20 B | 256 B |
| Columns, 3 rows, grouped | 20 B | 224 B | 256 B |
| Tuple, 64 rows | 1,472 B | 1,472 B | 1,536 B |
| Columns, 64 rows, independent | 160 B | 160 B | 256 B |
| Columns, 64 rows, grouped | 160 B | 1,472 B | 1,536 B |

Grouped escape retains its whole original allocation, including sibling bytes
and alignment gaps. It does not claim physical suballocation. The shared-shape
Export retains 307 B for Tuple or 264 B for columns, independently of Stage1.

Before explicit idle trim, mapped body capacity remains at collection high-water.
After trim, every profile has one live **4,096 B body page** and zero idle body
pages; Group/control metadata and reserved VA remain. Close cannot finish while
the Export owns that use. Releasing it lets close retire all Group/control/body
state. Setup and the 8-byte Group-vector capacity remain until Pool/Arena removal;
after removal both tracked heap classes and the Shape resource are empty.

## Qualification and recovery

Release `-O3 -DNDEBUG` and fully instrumented ASan/UBSan/LSan `-O1 -DNDEBUG`
pass all eight profiles. Checks are unconditional. The observer replaces every
ordinary/aligned/array/nothrow new/delete form with matched allocation/free,
tracks live requested/usable heap extent, and reads real allocator inventories.
Its fixed 128 KiB observation table is diagnostic storage, outside Stage1.

Outputs and source/dependency hashes live in ignored
`build/orbital-mvcc-metadata/allocation-profiles/receipt.json`; native observations
are `release.jsonl`. Root collection accounting adds the real catalog separately.
