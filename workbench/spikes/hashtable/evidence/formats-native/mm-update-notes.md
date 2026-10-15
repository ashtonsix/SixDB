# Captured multimap update path

This is a static call-path audit of the existing format capture, not a new timing
or a profile. The source manifest is
`58ca2ea54a6419c37610162759e880fd17a316066d9f580c419babee71402bd9`;
archive `fe56a8a6ca2e900fb1b62315b1a28539ed7c09fd37f66afe7f9d2408f6a2fe3e`.
The [Zen5](../formats-zen5/mm-update.json),
[Granite](../formats-granite/mm-update.json) and
[V2](../formats-neon/mm-update.json) receipts retain exact demangled names,
addresses, complete timed update intervals and acquisition excerpts. Full native
assembly remains in each host's artifact. Live format defaults subsequently
changed; these findings continue to describe the frozen capture.

At K40/A10, 7,168 keys initially contain either one value (4,778 keys) or two
(2,390 keys). The timed update appends one value to every key in the same shuffled
order. Candidate singleton values are inline; a second value acquires a four-word
bag. Existing two-value bags have room for the third. Abseil's ordinary
`InlinedVector<word,1>` actually has **two** inline words in this ABI: the native
loops select inline capacity 2 before comparing against size. Thus Abseil stays
inline for 4,778 updates and grows out of line for 2,390, the opposite branch mix
to the candidate. Its ordinary K40 key+bag slot is 32 bytes. This is a real
representation/footprint tradeoff, not equal allocation work with different
kernels.

The exact candidate's update medians are 83.289 / 109.510 / 149.864 ns on
Zen5 / Granite / V2, versus 14.201 / 17.510 / 20.775 for ordinary Abseil.
Candidate requested bytes plus metadata rise from 242,390 to 434,714 through
update. Abseil rises from 270,344 to 346,824 on x86 (eight bytes less at each
stage on V2). Initial candidate memory savings therefore reverse after these
appends. These are staged inventories, not peak/RSS measurements.

The outer `try_set` is inlined. Its immediately invoked **mutation-body lambda**
is outlined and called once per key, followed by an outlined **proposal lambda**.
Their full symbols end in `::'lambda'()::operator()() const` and the nested
optional-record lambda respectively; truncating names would misidentify the
boundary. The mutation lambda includes search/routing and commit, so this is not
a second hash lookup. For the exact arm, the relevant entries are:

| Host | Mutation body | Proposal | Bag acquire |
| --- | --- | --- | --- |
| Zen5 | `0x10c35f0` | `0x10c4ae0` | `0x15fbd0` |
| Granite | `0x10518f0` | `0x1052df0` | `0x1599d0` |
| V2 | `0xf1aaa0` | `0xf1be00` | `0x14f1a0` |

On x86 the exact mutation-body frame reserves 0x168 bytes plus saved registers;
the proposal reserves 0xb8. Bag acquisition reserves 0x40b8 plus six pushed
registers. V2 acquisition saves 0x60 and then subtracts 0x4000+0x90, giving
16,624 bytes total. The maximum-family temporary is 16,390 bytes, although these
fresh singleton promotions need only a 38-byte family-zero slot. **The frame is
not wholly zeroed or page-probed on x86**: only the selected slot width is passed
to memset. Frame size alone does not identify a timing cost.

The warmed acquisition path for a new slot still calls StreamPack
`layout::lengths_bytes`, `detail::input_alias`, `author::dispatch`, and an outlined
typed append continuation. It zeroes the temporary, then the append body memmoves
it into the packed payload; the bag values are written later during commit.
New-block allocation and existing-block growth are separate conditional paths
inside the same acquisition helper. The existing-room path skips acquisition,
but resolves block directory → family → prepared layout → slot address in the
proposal and again in commit. The latter resolution presently follows possible
allocation/directory relocation and is required by the owner contract. It must
not simply be removed while retaining a stale pointer.

StreamPack already exposes `author::append_unchecked(Access, ...)`. The spike
chose checked `st::append`; this is **not evidence of a missing author API**.
Using the existing author path requires proving the prepared fixed/full/immortal
shape and appropriate access type, current rank/extent and post-growth payload
capacity, disjoint temporary, exclusive nonsuspending ownership, and infallible
effects. Observer ordering and refusal behavior must remain correct. Reusing a
commit borrow would additionally require proving that no intervening operation
can relocate the directory or payload before the reservation completes.

A discriminating next experiment should preserve the full build/update/delete/
release history, source bytes, geometry, bag capacities, ordering and ordinary
Abseil control. Compare checked append with the existing typed author entry after
owner admission, initially retaining the same temporary and copy. This isolates
checked admission/dispatch from allocation policy and avoids simultaneously
claiming a win from a new bag representation. A separate diagnostic pass can
count existing-room, fresh-slot, grow and new-block events without instrumentation
inside the timed arm. Sampling/counters on the uninstrumented owner lifecycle
should test whether time actually concentrates in acquisition/dispatch, allocator
work, or the mutation/proposal bodies. If only fresh-slot updates improve while
existing-room updates remain stable, that supports the admission hypothesis; if
allocation remains dominant or the matched total does not move, the proposed
explanation is insufficient. Outlining and frame size are candidates for later
controls, not cycle estimates or established causes.
