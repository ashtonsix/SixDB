# Stable memory locators

The `locators` driver compares raw Part addresses with two forms of indirect
memory locator. The question is whether a value-only edit can publish new
segment bytes without repairing a directory whose logical bounds and
representation have not changed. Segment identity remains index, layer and
physical prefix. This is a caller experiment, not an Orbital locator API or a
portable format.

## Encoding and resolution

All three arms retain the 64-byte dirent: six eight-byte locators, a 32-bit
representation ID, 16-bit flags, 16-bit overflow rank, 32-bit overflow block
and 32-bit interpretation revision. Flag bit 2 (`4`) selects indirect memory
locators; bit 0 indicates presence and bit 1 indicates overflow. The revision
is a layout compatibility cross-check. Orbital's selected version and snapshot
cut remain outside the dirent and advance on value-only publication.

| Arm | Eight-byte locator | Additional per-segment storage |
| --- | --- | --- |
| `raw-address` | Native Part byte address | None for locators. |
| `indirect-full-cell` | Address of an immutable reference cell | 48-byte owner plus one 24-byte `{ObjectId, Part ordinal}` cell per Part. |
| `indirect-shared-object` | Address of an immutable ordinal cell | 48-byte owner containing the shared ObjectId, plus four bytes per Part. |

Sizes above are requested storage on the measured 64-bit targets; allocator
rounding and RSS are not included. The shared ObjectId is per segment, not an
index-wide registry. Both indirect arms use the same eight-byte token in inline
slots and in the existing StreamPack overflow list. Every segment realization
retains its cell owner. Compatible versions reuse it; a new Part count or
locator mode requires a new set. There is no update to a cell's target address.

A read first selects the directory, schema and segment under the same Orbital
Core and Scope. The indirect resolver compares the supplied token with the
address expected by that selected segment's proof before dereferencing any
supplied address. It validates the named Object and ordinal, then obtains bytes
from the already selected Part. The locator does not choose a newer version.
Capture admission rejects cells belonging to another segment, even when its
Part count and geometry match.

The publisher compares logical bounds and descriptors separately from segment
changes. A value-only edit preserves interpretation and cell addresses, so it
publishes five segment Objects and leaves directory, hash and overflow Objects
unchanged. Key, bound, Part-count or interpretation changes still repair the
affected directory normally. The raw-address control retains its original
pointer-load read path, so its comparison is not charged the indirect resolver.

## Histories checked

The correctness panel crosses three populations, two summary grains (64/256),
zero/four auxiliary Parts and all three locator modes. It derives expected
segments and summaries independently from records at all five layers. It checks
value replacement, forced byte copying, key movement, extension to 100,000
schema definitions, interval deletion, complete removal and recreation.

An old Scope spans the edits, including late discovery and a prepared read
finished afterwards. For indirect locators the checks prove that byte addresses
and actual Object versions change while the directory realization and locator
values stay unchanged. Wrong-ordinal and foreign-Object tokens are rejected.
Old tokens are rejected against a recreated segment while its earlier Scope
still protects the old cells.

A selected summary Part also escapes both its Scope and the Index. Its byte
contents, original Part ID, extent and retained interpretation/cell proof remain
valid after teardown; releasing it settles all tracked metadata. A selected
Part retains the complete geometry and cell owner, not siblings' native payloads.
This is a retention cost, not a minimal proof encoding.

An escaping directory Part does not retain a transitive snapshot of all child
Objects. Combining its bytes with an arbitrary fresh Scope is not established
by this experiment. Keep the original coherent Scope/cut for navigation, or
explicitly revalidate the routing association. Locator equality alone proves
neither age nor graph coherence.

## Measurement boundary

Each performance fixture has 4,096 records, four auxiliary Parts and 64 value
replacements with a reader retained across the burst. Dense and sparse J=0
populations contain duplicate logical runs of 129 records; the many-prefix J=2
population uses one record per distinct two-byte logical/physical prefix.
The read consumes a layer-4 directory lookup, summary and four auxiliary Parts,
creating a fresh Scope per query. Trial order rotates the three arms. Identical
queries and checksums are required; all arms run through the same publisher.

Reported update time is a burst average, not point-update p50/p99 or service
throughput. Phase counters distinguish author-model work, segment image and
capture, directory work, prepare and installation. Copied bytes count native
Image-to-Part copies, not every staging or equality-read byte. The tracked PMR
resource counts requested Object/proof metadata, including cell owners and
retained histories; it excludes native Arena payloads, hash backing, mutable
author maps, ordinary scratch allocations and RSS. `live_locator_bytes` counts
the current cell sets once per segment. It is not a complete index-space metric.

These are local native-memory publications. Mapped backing, opaque resource
identifiers, portable byte reopen and durable resource resolution remain open
implementation questions in the [coverage audit](PROGRESS.md).

## Native results

The final source capture is worker group `20261020T095307Z-57e5ea74`.
All three targets passed the complete locator check before measurement. The
measured C++ and runner bytes match the implementation in `8b368c4e`; the archive
retains the exact capture and compiler receipt. The selected
[Zen 5](evidence/locators-zen5/), [Granite Rapids](evidence/locators-intel/) and
[Neoverse V2](evidence/locators-neon/) exports retain every trial and counter for
all arms and both summary grains. Earlier prototype locator sweeps are
superseded diagnostics, not additional retained comparisons.

Five-trial medians, summary grain 64, K=8, 16,384 reads and 64 updates per trial:

| Target / population | Read ns, raw → compact | Update µs, raw | Update µs, full cell | Update µs, compact |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 / dense | 190 → 209 | 15.73 | 10.41 | 10.16 |
| Zen 5 / sparse | 216 → 234 | 33.43 | 16.87 | 17.69 |
| Zen 5 / many-prefix | 540 → 567 | 68.18 | 23.84 | 25.23 |
| Granite Rapids / dense | 208 → 228 | 17.64 | 12.31 | 12.02 |
| Granite Rapids / sparse | 241 → 258 | 35.21 | 17.39 | 17.43 |
| Granite Rapids / many-prefix | 523 → 558 | 55.62 | 25.02 | 24.61 |
| Neoverse V2 / dense | 290 → 321 | 21.18 | 14.71 | 13.94 |
| Neoverse V2 / sparse | 323 → 347 | 44.64 | 22.89 | 23.18 |
| Neoverse V2 / many-prefix | 583 → 632 | 70.23 | 32.21 | 34.42 |

The compact arm cuts update time by 32–63% in this panel, charging a 5–10.5%
read increase. It does not dominate the full-cell arm: full cells are faster
on several sparse/many-prefix cases. Their metadata cost is much larger.
Grain 256 remains in the evidence rather than being selected away: on Zen 5
the compact dense update is 9.96 µs, but the many-prefix update rises to
36.47 µs. Neither smaller locator cells nor a larger summary chunk determines
the complete publisher's performance by itself.

For the many-prefix grain-64 burst, all targets report the same logical work:

| Counter | Raw | Full cell | Compact |
| --- | ---: | ---: | ---: |
| Native copied bytes per update | 32,123.25 | 5,240 | 5,240 |
| Published Objects per update | 14.75 | 5 | 5 |
| Current locator storage, bytes | 0 | 3,546,504 | 1,083,284 |
| Tracked metadata after burst, bytes | 5,728,560 | 9,143,592 | 6,680,372 |

Both indirect arms create zero new cell bytes, dirent bytes and hash-owner bytes
during the value burst. Compact cells reduce locator storage by 69.5% relative
to full cells. They still add storage relative to raw addresses. The escaped
Part check also demonstrates that complete cell/geometry owners can remain
pinned by a single selected Part; a separate colocation/retention study is still
needed before choosing an ownership recipe.

In the Zen 5 many-prefix panel, median directory work falls from 35.14 µs/update
to 0.043 µs for compact cells. Remaining measured phases include 5.83 µs in the
author model, 6.03 µs in image construction and 7.86 µs in segment capture;
prepare/install are 0.32/0.06 µs. These are separately timed nested portions,
not a complete additive accounting of the wall-clock median. They identify
the next costs to investigate without attributing them to the directory concept.
There is still considerable prototype overhead, and no matched LMDB comparison
has been executed by this driver.

All seven drivers passed the final local Release capture
`20261020T094921.401815Z`; all seven also passed ASan/UBSan in 25.84 seconds
with `-O1 -g1 -fsanitize=address,undefined -fno-omit-frame-pointer`. The fixture
uses a synchronous writer and retained readers, including Loom execution on
its existing path. It does not qualify cross-worker reference-count concurrency.
