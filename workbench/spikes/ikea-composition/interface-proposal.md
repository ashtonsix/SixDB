# Proposed Ikea interface

**Status:** proposal (2026-10-22). Nothing here is implemented yet.

**Brief.** Ashton asked for a coherent interface across Ikea's modules before
Frame, posting lists and string collections put consumer weight on them:

- Disruption is allowed.
- Accidental differences are not.
- The interface should be as simple and unsurprising as it can be without
  costing performance.

**Starting point.** This proposal builds on the
[interface-cohesion audit](interface-cohesion.md) and
[the interfaces guide](../../../ikea/docs/interfaces.md). It keeps their correct
distinctions: coordinates, grain, issued bytes versus logical destinations, and
parent framing. Where the audit kept a difference only because no consumer had
asked for a change yet, this proposal decides the question.

**The test applied to every difference.** Each difference must carry meaning a
reader needs. Otherwise it goes. Native bodies, carriers and traversal are not
touched. All of this is names, types and contracts at the boundary.

**Ashton's direction (2026-10-22).**
- **Headless, always.** Data carries no per-block descriptor or metadata header.
  The parent states a layout once, for example in a schema shared by about 100k
  blocks, and each block group carries only a short schema id.
- **Targets.** SixDB must eventually run on a very wide range of targets,
  including PowerPC. For now, make simplifying assumptions, aggressively if
  useful.
- **Vocabulary.** Single nouns and a small vocabulary. Where differences exist,
  they must not be accidental.

§1, §4, §6 and §8 apply this.

## 1. One lifecycle, one word per step

Every module moves through the same steps. Some modules skip steps, but no
module uses a step's word for a different step.

| Step | Noun | Verb | What it establishes |
| --- | --- | --- | --- |
| Describe | `layout` | `layout::make(...)` (runtime) or a layout type (compile time) | The byte law: how values map to bytes. Independent of addresses. |
| Place | `view` / `const_view` | `view::attach(layout, storage...)` | That law at actual addresses and extents. Checks metadata, not contents. |
| Admit contents | — | `view.validate()` | That recovered bytes obey the law. Only where contents can be invalid. |
| Plan | `plan` (`read_plan`, `write_plan` where both exist) | `plan::make(layout, ...)` | Storage-independent preparation: maps, groups, routes, chains. |
| Bind | `reader`, `writer`, `transfer` | `bind_reader(plan?, view)` etc. | A reusable operation over a view. It borrows both. |
| Erase | `erased_reader<U>`, `erased_writer<U, Summary>` | `.erase<...>()` | A runtime-dispatched handle to a whole bound operation. |
| Run | — | `get`, `read`, `filter`, `indexof`, `set`, `replace`, `initialize`, ... | One command. Checked by default; `_unchecked` skips command checks. |

**Kept deliberately small.** There are three operation nouns: `reader`, `writer`
and `transfer`. Each is optionally `erased_`.
- **Filtering is a verb, not a noun.** `reader.filter<Op>(...)` in every module
  replaces TuplePack's `field_filter` and `bind_filter`. A TuplePack reader
  over a one-code plan keeps its direct encoded comparison. Any other plan
  rejects the checked call with `error::map`.
- **Construction is a writer command, not a noun.**
  `writer.initialize(first, input)` replaces TuplePack's `constructor`,
  `construction_operation` and `bind_constructor`. A write plan covering every
  code selects the no-old-read body at plan time, as `constructor` does today,
  including its two-packet form above 64 codes. SeriesPack's writer already has
  `initialize`.

Rules that follow from the table:

- **`attach` only means placement.** It replaces `view::bind` (TuplePack,
  StreamPack) and `source::admit` (Bec256).
- **`bind` only means building an operation.** It replaces
  `prepared_mutation::bind`, `prepared::bind` and
  `composition::prepare(expr, count)`, which are operation constructors under
  plan words.
- **`make` only builds a layout or plan.** It replaces StreamPack's
  `layout::prepare`.
- **`_unchecked` is the only trusted marker.** `assume_valid` becomes
  `attach_unchecked`. This removes a real double-take: StreamPack today has
  `bind` (placement), `validate()` (contents) and `assume_valid` (placement
  only, despite the word "valid").
- **Each `_unchecked` entry sits beside its checked peer**, in the same
  namespace. StreamPack's `author::append_unchecked` and its siblings move
  next to `append`.
- **Infallible binds return the operation; fallible ones return
  `std::expected`.** This is already mostly true.

### Per module

| Module | Today | Proposed |
| --- | --- | --- |
| SeriesPack | `format<K, geometry, H>`; `view<F, const uint8_t>::attach`; `bind_decoder<U>(view)` → `decoder<U>`; `composition::prepare(describe(view), n)` → `prepared<E>`; `bind_mutation(view)` → `prepared_mutation<E>`; `.erase<U,S>()` → `mutation_operation<U,S>` | `layout<K, geometry, H>`; `const_view<L>::attach` / `view<L>::attach`; `bind_reader(view)` → `reader<E>` (typed: get/read/filter/indexof/sum); `.erase<U>()` → `erased_reader<U>`; `bind_writer(view)` → `writer<E>`; `.erase<U,S>()` → `erased_writer<U,S>` |
| TuplePack | `layout::make`; `view::bind`; `reader<N,R>::make` (plan); `bind_reader` → `read_operation`; `writer<N,R>::make`; `bind_writer` → `mutation_operation`; `constructor::make`; `bind_constructor` → `construction_operation`; `bind_transfer` → `unit_transfer`; `bind_filter` → `field_filter` | `layout::make`; `view::attach`; `read_plan<N,R>::make`; `bind_reader` → `reader` (with `filter`); `write_plan<N,R>::make` (all codes when no map is given); `bind_writer` → `writer` (with `initialize`); `bind_transfer` → `transfer` |
| StreamPack | `description` (options); `layout::prepare`; `view::bind`; `assume_valid`; `validate()`; free `value`/`append`/...; `author::*_unchecked`; `author::dispatch` | `layout::options`; `layout::make`; `view::attach`; `attach_unchecked`; `validate()`; free checked and `_unchecked` commands side by side; `author::visit(view, f)` for typed whole-consumer dispatch |
| Bec256 | `source::admit`; `source::assume_valid`; `destination{span}`; `prepare(block, pop)` → `encoding` | `const_view::attach(storage, bytes, population)`; `attach_unchecked`; `view{span}`; `encoding::make(block, pop)`. Free `encode`/`decode` stay. |
| Intervals | Source structs (`direct_source{data, count}`, `from_cuts`, `sparse_*`); free `locate`/`reverse`/`overlap`/`range` | Unchanged. Intervals sources are already a combined layout+view over borrowed arrays, and the free functions are the one-shot calls. Only names in §3 and §6 change. |
| Hashtable | `table<W,b,V,...>` owner; runtime `descriptor<W,V>`; `descriptor::prepared_read`, `native_read::basic_view` | `table` unchanged. `descriptor` → `erased_table<W,V>`, `prepared_read` → `reader`, `native_read` → `native_reader`, and its inner `basic_view` → `staged_reader`. "Descriptor" and "view" mean something else everywhere else in Ikea. |

SeriesPack's runtime-width binder (`bind_decoder(dense_input)`) becomes
`bind_reader(dense_input) -> expected<erased_reader<U>, error>`. The width is
only known at run time there, so the result is erased from the start.

**Why `layout` for SeriesPack's compile-time format.** "Format",
"representation", "description" and "descriptor" currently name four related
things across modules. One noun, `layout`, means the byte law everywhere.
SeriesPack's runtime recovery struct `representation` becomes
`layout_value` (`K`, `H`, geometry), checked against a compile-time layout by
`attach`.

### What a consumer sees

Before, SeriesPack and TuplePack read one value in different dialects:

```cpp
auto sv = sp::view<F, std::uint8_t>::attach(n, planes);        // placement
auto sr = sp::bind_decoder<std::uint32_t>(*sv);                // erased read op
auto tv = tp::view::bind(*layout, bytes, n, stride);           // placement
auto plan = tp::reader<8>::make(*layout, map);                 // plan
auto tr = tp::bind_reader(*plan, *tv);                         // read op
```

After:

```cpp
auto sv = sp::view<L>::attach(n, planes);
auto sr = sp::bind_reader(*sv).erase<std::uint32_t>();
auto tv = tp::view::attach(*layout, bytes, n, stride);
auto plan = tp::read_plan<8>::make(*layout, map);
auto tr = tp::bind_reader(*plan, *tv);
```

## 2. Bytes and views

- **Borrowed storage is `std::byte` in every public signature.** This covers
  view storage, descriptor `encode`/`decode`, effect targets and Hashtable value
  bytes. Inside kernels, `uint8_t` arithmetic stays as it is. A single
  `ikea/bytes.h` supplies `to_u8(span<std::byte>)` for bodies. Both are
  aliasing byte types, so this changes types, not code. The emitted-code receipt
  in §9 checks that.
- **`view` is writable and `const_view` is read-only in every module.** Today
  SeriesPack's `view<F>` defaults to read-only, while TuplePack's and
  StreamPack's `view` is writable. That's a silent double-take.
  `basic_view<..., Byte>` stays as the template under both aliases.

## 3. Coordinates and selection

The coordinate words in the interfaces guide stay: row, rank, window bit, packet
byte, block bit and plane byte. What changes is that the same idea gets the same
spelling.

**Selections share one shape, not one storage format.** Each module keeps the
storage it needs, but the type is called `selection` everywhere, with one API:

```cpp
struct selection {                      // per module; storage differs
    static selection all() noexcept;
    static selection none() noexcept;
    static selection bits(std::size_t first, std::span<const Word> words) noexcept;
    bool is_all() const noexcept;
    bool is_none() const noexcept;
    bool covers(std::size_t first, std::size_t count) const noexcept;
    bool contains(std::size_t i) const noexcept;
};
```

| Module | `Word` and origin rule |
| --- | --- |
| SeriesPack | `uint16_t`, origin divisible by 16. Its native region grain stays visible. |
| TuplePack | `uint64_t`, any origin. TuplePack gains `none()`. |
| Intervals and StreamPack | `std::byte`, exact tail. `inverted` becomes a separate constructor, `complement(first, bytes)`. |

StreamPack's `select(view, span<const uint16_t> ranks, ...)` keeps its name. A
rank *list* is a different thing from a bitmap, and the parameter type says so.

**A shared 256-position block.** Frame blocks are 256 rows, Bec256 bodies are
256 positions, and StreamPack blocks hold 256 ranks. Frame already wrote its own
`row_selection = std::array<u64, 4>` with popcount, rank, empty and full
helpers, and its name collides with SeriesPack's. The proposal is
`ikea/block256.h`:

```cpp
namespace ikea {
struct block256 { alignas(32) std::array<std::uint64_t, 4> words; };
unsigned count(const block256&);
bool empty(const block256&);
bool full(const block256&);
unsigned rank(const block256&, unsigned position);  // set bits before position
std::uint16_t region16(const block256&, unsigned first);               // SeriesPack grain
std::uint64_t window64(const block256&, unsigned first, unsigned count);  // TuplePack and filters
}
```

`bec256` decodes into and encodes from `block256`, replacing
`std::array<std::byte, 32>`. Each module's `selection::bits` accepts a
`block256` at a block origin. This is the one addition, not a rename. Frame and
a posting-list consumer are the witnesses, and the conversion paths need the
emitted-code check.

## 4. Effects: one protocol

Today there are three:
- `ikea::write_journal`, plane-relative and still used in SeriesPack tests;
- `ikea::source_write_journal`, source-qualified;
- Hashtable's `mutation_damage {owner, part, base, extent, span}`, with a
  separate context protocol.

The coverage hook itself is an unnamed duck type. Capacity comes in four call
shapes and two errors.

Proposed:

1. **Three nouns: `effects`, `journal` and `journal::entry`.** These replace
   `byte_write`, `owner_write`, `write_journal`, `source_write_journal`,
   `no_coverage`, `mutation_damage` and the unnamed "Coverage" duck type.
   Everything lives in `ikea/effects.h`:

   ```cpp
   namespace ikea {
   template <class E, class Source>
   concept effects = requires(E& e, const Source& s, unsigned plane, std::size_t offset,
                              std::size_t size) {
       { e.before(s, plane, offset, size) } noexcept;
   };
   struct no_effects { /* before(...) does nothing */ };
   struct journal {
       struct entry { const void* source; unsigned plane; std::size_t offset, size; };
       std::span<entry> storage;
       std::size_t used = 0;
       // before(...), remaining(), entries(): as source_write_journal today
   };
   }
   ```

   Every `_unchecked` mutation is constrained on `effects`. Checked mutations
   and erased writers take `ikea::journal&`. Parameters are already named
   `effects` almost everywhere, so the concept takes that name.
2. **`plane` is the module's own plane-enum ordinal.** That's already true for
   StreamPack's `plane`. Hashtable's `owner_part` becomes its plane enum, and
   `mutation_damage` becomes `journal::entry`, with `base`/`extent` resolved
   through the source like every other module. Hashtable's `enter`/`prepare`
   context stays, because it is an owner protocol no other module has. Its
   `before` satisfies `effects`.
3. **No plane-only journal.** A caller that knows its single source ignores the
   `source` field. Two journal types differing by one field caused the alias
   confusion the audit fixed.
4. **One capacity spelling.** Every bound operation answers
   `effect_capacity(command...) -> std::size_t` in *additional records* for that
   command, as a conservative bound:

   | Module | Today | Proposed |
   | --- | --- | --- |
   | SeriesPack | `effect_capacity(first, count, sel)` and `construction_effect_capacity()` | Kept, the second renamed `effect_capacity_initialize()` |
   | TuplePack | `effect_capacity()` per active row | Becomes per command: `effect_capacity(first, count, sel)` |
   | StreamPack | `layout.append_effect_capacity()` and `pack_effect_capacity(n)` | Free `effect_capacity_append(view)`, `effect_capacity_pack(view)` and so on, beside the commands |
   | Bec256 | — | `encoding.effect_capacity()` (0 or 1) |
5. **One error for "journal too small": `error::effects`.** Today SeriesPack
   and TuplePack report this as `error::capacity`, and StreamPack and Bec256 as
   `error::effects`. `capacity` then means storage too small, everywhere.

## 5. Errors

Each module keeps `enum class error` and `std::string_view describe(error)`.
Shared condition names mean one thing in every module:

| Name | Meaning |
| --- | --- |
| `range` | The command's coordinates are outside the view |
| `value` | An input value doesn't fit the layout |
| `capacity` | Storage is too small |
| `effects` | The journal is too small |
| `overlap` | Command, effect or storage spans alias where they must not (replaces `alias` in StreamPack and Intervals) |
| `description` | The layout or encoded description is invalid or mismatched |
| `contents` | Validation found bytes that don't obey the law |

Module-specific conditions (`retired`, `truncated`, `population`, `stride`, ...)
stay. `describe` means only "error to text": SeriesPack's
`composition::describe(view)` becomes `author::expression(view)`, and
`describe_preset<F>` becomes `suggest_placement<L>`.

The shared CPS chain gets its own `ikea::author::chain_error { stages, stage }`,
so Bec256's enum no longer carries `range` ("Invalid pipeline stage count") and
`value` ("Invalid pipeline stage function").

Hashtable's statuses (`mutation_status`, `lookup_status`) stay. They are
outcomes, not failures. The `std::errc` return in its storage policy
(`hashtable/storage.h:41`, `trim_result`) becomes a `hashtable::error`.

## 6. Persisted descriptions

**Contract: headless.** No Ikea data block carries a descriptor, tag or
version.
- A layout's encoded description is written once, by the parent, wherever the
  parent chooses. That might be a schema covering about 100k blocks, with a
  short schema id per block group.
- The description encodes the *byte law only*.
- Placement (origins, strides, offsets) and logical count belong to the parent's
  interpretation, which already persists plane identities and offsets.
- Bec256 bodies are already headless.
- Per-block data that a block genuinely needs, such as StreamPack's assigned
  count and extent in its control plane, is data rather than description. It
  stays in caller-placed planes, never in a mandatory header.

Today:

- SeriesPack's 40-byte descriptor carries count and three strides, but not
  offsets. Placement is split across two homes.
- TuplePack carries only the unit layout. This is already correct.
- StreamPack's 8-byte `layout::encode` has no tag or version, and has a
  `legacy` flag variant.

**One envelope for the description itself**, implemented once in
`ikea/description.h`. It frames the encoded layout inside the parent's schema,
not any data block. At a few bytes per layout per schema, its cost is
negligible:

```text
bytes 0..1  module tag ('SP', 'TP', 'SK', ...)
byte  2     law version
byte  3     body length in bytes
bytes 4..   module-owned body, little-endian, reserved bits zero
```

Every layout has `encoded_size()`,
`encode(span<std::byte>) -> expected<size_t, error>` and
`static decode(span<const std::byte>) -> expected<layout, error>`. Decode
requires the exact length and rejects unknown tags, versions or reserved bits.
For SeriesPack, `decode` yields a `layout_value`, which `view<L>::attach`
checks for an exact match.

The bodies stay module-owned and differ:
- SeriesPack: K, H and geometry, so 3 bytes;
- TuplePack: the unit bytes and code triples;
- StreamPack: its schema.

Bec256 has one fixed law and no description. Hashtable's runtime owner has no
wire form.

The StreamPack `legacy` geometry goes if no persisted bytes use it. The cohesion
audit recorded that no persisted bytes were retagged. That needs confirming
with its owner before deletion.

## 7. Namespaces and headers

| Namespace | Holds |
| --- | --- |
| `ikea::<module>` | Ordinary checked and `_unchecked` calls, and the nouns in §1 |
| `ikea::<module>::author` | Everything from `author/*.h` except native carriers: expressions, substitution, erased operations, chains, routes, dispatch |
| `ikea::<module>::native` | Register carriers and native bodies, from `author/native.h` |
| `ikea::<module>::detail` | Private |
| `ikea::author` | Shared author mechanisms: the chain, from `ikea/author/chain.h` (moved out of `ikea/detail/native_chain.h`) |

**What moves:**
- SeriesPack and TuplePack `composition::` → `author::`.
- Bec256's top-level author entries → `author::`.
- SeriesPack's `chain` and `summaries` → `author::`.
- Public entries declared in `detail/` headers (SeriesPack's `bind_decoder` in
  `detail/placed_read.h` and `detail/dense_read.h`) move to public headers.
- File names match their main type. Today `read_operation.h` holds `decoder`.
- The leftover Boundlist alias `namespace bl = ikea::intervals` in
  `intervals/sparse.h` goes.

## 8. Targets

Every public entry exists on every supported target. ISA selects bodies, not
availability. Today `bind_mutation` and `prepared::sum` exist only under
`__aarch64__ || __AVX2__`, so a baseline x86 build silently loses mutation.

**Decision:** use simplifying assumptions now, and keep the design open to
eventual wide targets, including PowerPC.
- Ikea's minimum target is x86-64-v3 or armv8-a+simd, matching
  [BUILDING.md](../../../BUILDING.md). One `#error` in `ikea/config.h` enforces
  it, and the per-entry `#if` guards go.
- The interface stays target-neutral: no ISA type, carrier width or intrinsic
  appears in an ordinary signature. A later scalar or other-ISA body can then
  slot in without an interface change.
- Native carriers in `native::` are ISA-shaped by design, and stay so.

## 9. Deliberately unchanged

- **Value types, native carriers, packet shapes, traversal and grain.** The
  Intervals qualification and filter-composition results show that generic
  wrappers cost real work.
- **Coordinate kinds.** Rows and ranks stay distinct words. A native TuplePack
  mask stays indexed by packet byte, translated through the plan's ordering.
- **Summary laws.** SeriesPack's `Summary` parameter and TuplePack's
  observation brackets answer different maintenance questions. Revisit them with
  Frame's aggregates rather than unifying now.
- **Hashtable stays an owning container.** It shares only bytes, effects,
  errors and naming with the borrowed packs.

## 10. Migration and verification

**Scale.** Outside `ikea/include`, the renamed names appear about 750 times in
191 files (a `git grep` count). Most are in Ikea tests and examples, and in the
ikea-composition, hashtable and streampack spikes. The largest:

| Name | Hits |
| --- | ---: |
| `composition::` | 183 |
| `view::bind` | 135 |
| `row_selection` | 97 |
| `bind_decoder` | 63 |
| `mutation_damage` | 53 |

The only callers in maintained modules outside Ikea are
`loom/examples/allocation.cpp`, `loom/test/hints.cpp` and
`orbital/test/preparation.cpp`. `engine/src/directory.cpp` uses only Intervals
calls that don't change. Frame's spike
code is owned by Ikea LEAD and the Frame thread. Its migration is theirs to
schedule, and I'd hand them a mechanical rename list.

**Order.** Each step is one commit and builds green.

1. Rename steps 1, 2, 5 and 7: verbs, nouns, namespaces, errors and files.
   This is mechanical, via a scripted rename, with no deprecated aliases kept.
2. Bytes (§2) and views' writable defaults.
3. Effects (§4), including Hashtable damage → `owner_write` and capacity
   spelling.
4. Descriptions (§6). This changes SeriesPack's and StreamPack's descriptor
   bytes, so the frozen wire fixtures get new versions.
5. `block256` (§3) with the Frame and posting-list witnesses.
6. Targets (§8).

**Checks.** For steps 1 to 3 and 6:
- the emitted-code receipt from the cohesion audit (`cohesion-codegen.py`)
  must show equal functions, constants and unwind sections on V2 NEON, Zen5
  AVX2 and Zen5 AVX-512;
- the full-scope `ikea_validate` suite must pass on a 4xlarge worker.

Steps 4 and 5 change bytes or add code, so they get independent wire fixtures
and Frame consumer timings instead.

**Docs.** The interfaces guide, module guides and examples are rewritten in the
same commits, so no doc describes the old names.

## Decisions

All three were settled by Ashton's direction above:
- **Minimum target:** x86-64-v3 / armv8-a+simd for now (§8).
- **Descriptors:** headless. SeriesPack's count and strides leave its
  description (§6).
- **Vocabulary:** one noun per idea, so `layout` replaces `format`,
  `representation`, `description` and `descriptor`, and operation and effect
  nouns shrink (§1, §4).
