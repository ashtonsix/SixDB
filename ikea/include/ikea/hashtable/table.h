#pragma once
#include "mutation.h"
#include "storage.h"
#include "lookup.h"
#include "detail/bucket.h"
#include "detail/control.h"
#include "detail/read-batch.h"
namespace ikea::hashtable {
namespace author {
struct table_access;
}
// Exact packed primary buckets with a Swiss overflow owner.
// W, b, E specialize a caller descriptor's operation; B/T never change with E.
// Primary capacity stays fixed: ordinary insertion grows only Swiss overflow.
// The defaults are portable; measured_encoding selects a deployment recipe.
template <unsigned W, unsigned b, unsigned V, unsigned E = (W - b),
          search_kernel Kernel = search_kernel::generic, class Storage = heap_storage_policy>
class table : private detail::bucket_ops<W, b, V, E, Kernel> {
    using bucket_base = detail::bucket_ops<W, b, V, E, Kernel>;
    friend struct author::table_access;
    using allocation_context = typename Storage::context;
    using allocation = typename Storage::buffer;
    static_assert(std::is_nothrow_move_constructible_v<allocation> &&
                      std::is_nothrow_move_assignable_v<allocation>,
                  "storage buffer moves must not throw after allocation preflight");
    alignas(64) inline static constexpr typename bucket_base::value_type missing_value_{};

  public:
    using storage_policy = Storage;
    using bucket_base::key_width;
    using bucket_base::bucket_bits;
    using bucket_base::K;
    using bucket_base::bitmap;
    using bucket_base::B;
    using bucket_base::T;
    using bucket_base::N;
    using bucket_base::encoding;
    using bucket_base::value_rows;
    using bucket_base::stride;
    using bucket_base::bucket_alignment;
    using bucket_base::value_offset;
    using bucket_base::flags_offset;
    using bucket_base::overflow_offset;
    using bucket_base::logical_bucket_count;
    using bucket_base::bucket_count;
    using bucket_base::key_bytes;
    using bucket_base::tuple_bytes;
    using typename bucket_base::value_type;
    using typename bucket_base::mix_type;
    using typename bucket_base::location;

    struct lookup_value {
        value_type value{};
        bool found = false;
    };
    struct borrowed_value {
        const std::byte* address = missing_value_.data();
        bool found = false;
    };
    struct probe {
        typename mix_type::result hash;
        const std::byte* primary;
    };
    // Private-owner mutation cursor. The enclosing owner keeps exclusive
    // access and invalidates it on every intervening mutation/preparation.
  private:
    struct write_position {
        typename mix_type::result hash;
        std::size_t slot;
        bool primary, present;
    };

  public:
    enum class staged_phase : std::uint8_t { primary, controls, tuple, value };
    struct staged_probe {
        probe first;
        word key;
        std::size_t group = 0, step = 0;
        unsigned candidates = 0, slot = 0;
        staged_phase phase = staged_phase::primary;
        bool empty_group = false;
    };

  private:
    using bucket_base::present;
    using bucket_base::occupied;
    using bucket_base::live_matches;
    using bucket_base::has;
    using bucket_base::mark;
    using bucket_base::primary_matches;
    using bucket_base::primary_find;
    using bucket_base::consume_primary;
    using bucket_base::write_value;
    using bucket_base::tuple_key;
    using bucket_base::consume_tuple;
    using bucket_base::vacancies;
    using bucket_base::write_find;
    using typename bucket_base::row_mask;
    using bucket_base::matching_rows;
    using bucket_base::occupy;
#if defined(__aarch64__)
    using bucket_base::equal64_neon;
#endif
    static std::size_t primary_bytes() {
        const auto bytes = logical_bucket_count * stride;
        if (bytes > std::numeric_limits<std::size_t>::max() - 63 ||
            logical_bucket_count * N > std::numeric_limits<std::size_t>::max())
            throw std::length_error("bucket geometry exceeds addressable owner");
        return std::size_t(bytes);
    }
    allocation_context* memory_ = nullptr;
    allocation primary_, control_, slots_;
    std::size_t groups_ = 0, spill_size_ = 0, deleted_ = 0, size_ = 0;
    mix_type mix_;
    struct uninitialized_image_tag {};
    // Author-only unpublished construction. The author must initialize every
    // assigned primary byte, including rounded padding, before any read/escape.
    table(uninitialized_image_tag, word seed, allocation_context* memory)
        : memory_(memory), primary_(primary_bytes(), memory, false), mix_{seed & mask<W>} {}
    table(const table& source, allocation_context* memory)
        : memory_(memory), primary_(duplicate_buffer<Storage>(source.primary_, memory)),
          control_(duplicate_buffer<Storage>(source.control_, memory)),
          slots_(duplicate_buffer<Storage>(source.slots_, memory)), groups_(source.groups_),
          spill_size_(source.spill_size_), deleted_(source.deleted_), size_(source.size_),
          mix_(source.mix_) {}
    static constexpr std::uint8_t empty = detail::control_empty, tombstone = detail::control_tombstone;
    static constexpr std::size_t absent = ~std::size_t{0};
    template <class Consume>
    [[gnu::always_inline]] static void consume_located(Consume&& consume, const value_type& value,
                                                       location at) {
        if constexpr (std::is_invocable_v<Consume, const value_type&, location>)
            consume(value, at);
        else
            consume(value);
    }
    std::uint8_t* bucket(word id) {
        return ikea::to_u8(primary_.data) + id * stride;
    }
    const std::uint8_t* bucket(word id) const {
        return ikea::to_u8(primary_.data) + id * stride;
    }
    std::size_t find_swiss(word key, const typename mix_type::result& h) const {
        if (!groups_)
            return absent;
        std::size_t g = h.swiss & (groups_ - 1), step = 0;
        for (;;) {
            const std::uint8_t* controls = ikea::to_u8(control_.data) + g * 16;
            auto candidates = equal_controls(controls, h.fingerprint());
            while (candidates) {
                const auto i = std::countr_zero(candidates);
                candidates &= candidates - 1;
                const auto index = g * 16 + i;
                if (tuple_key(ikea::to_u8(slots_.data) + index * tuple_bytes) == key)
                    return index;
            }
            // All matches in this group precede the empty-group termination.
            if (equal_controls(controls, empty))
                return absent;
            g = (g + (++step)) & (groups_ - 1);
        }
    }
    [[gnu::always_inline]] std::size_t find_swiss_read(
        word key, const typename mix_type::result& h) const {
        constexpr auto width = detail::swiss_read_groups;
        if constexpr (width == 1) {
            return find_swiss(key, h);
        } else {
            if (groups_ < width)
                return find_swiss(key, h);
            const auto mask = groups_ - 1;
            auto group = h.swiss & mask;
            std::size_t step = 0;
            for (;;) {
                const auto packet = detail::compare_swiss_read<width>(
                    ikea::to_u8(control_.data), group, step, mask, h.fingerprint());
                auto candidates = packet.candidates;
                while (candidates) {
                    const auto lane = std::countr_zero(candidates);
                    candidates &= candidates - 1;
                    // Reconstruct only an actual candidate's slot. Keeping an
                    // array of all group addresses spills on every miss.
                    const auto slot = detail::swiss_read_slot<width>(group, step, lane, mask);
                    if (tuple_key(ikea::to_u8(slots_.data) + slot * tuple_bytes) == key)
                        return slot;
                }
                if (packet.empties)
                    return absent;
                // Most probes terminate in this packet. Do continuation work
                // only when needed; mutation/staged cursors stay unchanged.
                group = detail::swiss_read_group(group, step, width, mask);
                step += width;
            }
        }
    }
    struct overflow_value {
        value_type value{};
        bool found = false;
    };
    using overflow_result = overflow_value;
    static bool overflow_found(const overflow_result& result) {
        return result.found;
    }
    // Keep TuplePack's prepared controls and Swiss probe state out of the
    // primary loop's live register set. Return the value, so the caller can
    // still consume its primary value directly in the success branch.
    [[gnu::noinline, gnu::pure]] overflow_result read_overflow(word key, word swiss) const {
        const typename mix_type::result h{0, 0, swiss};
        const auto found = find_swiss_read(key, h);
        {
            overflow_result result;
            if (found != absent) {
                consume_tuple(ikea::to_u8(slots_.data) + found * tuple_bytes,
                              [&](const auto& value) { result.value = value; });
                result.found = true;
            }
            return result;
        }
    }
    // Return an address so the caller can defer payload loads. Keep the same
    // Swiss traversal as ordinary reads, outside the primary loop's registers.
    [[gnu::noinline, gnu::pure]] const std::uint8_t* borrow_overflow(word key, word swiss) const {
        const typename mix_type::result h{0, 0, swiss};
        const auto found = find_swiss_read(key, h);
        return found == absent ? nullptr : ikea::to_u8(slots_.data) + found * tuple_bytes + key_bytes;
    }
    void insert_swiss_unchecked(word key, const value_type& v, const typename mix_type::result& h) {
        std::size_t g = h.swiss & (groups_ - 1), step = 0;
        for (;;) {
            std::uint8_t* controls = ikea::to_u8(control_.data) + g * 16;
            auto available = equal_controls(controls, empty) | equal_controls(controls, tombstone);
            if (available) {
                const auto i = std::countr_zero(available);
                const auto index = g * 16 + i;
                deleted_ -= controls[i] == tombstone;
                store<key_bytes>(ikea::to_u8(slots_.data) + index * tuple_bytes, key);
                if constexpr (V)
                    std::memcpy(ikea::to_u8(slots_.data) + index * tuple_bytes + key_bytes, v.data(), V);
                controls[i] = h.fingerprint();
                ++spill_size_;
                return;
            }
            g = (g + (++step)) & (groups_ - 1);
        }
    }
    void rehash_swiss(std::size_t groups) {
        // Prepare both fallible allocations before touching the live owner.
        allocation next_control(groups * 16, memory_),
            next_slots(groups * 16 * tuple_bytes, memory_);
        std::memset(next_control.data, empty, groups * 16);
        allocation old_control = std::move(control_), old_slots = std::move(slots_);
        const auto old_groups = groups_;
        control_ = std::move(next_control);
        slots_ = std::move(next_slots);
        groups_ = groups;
        spill_size_ = deleted_ = 0;
        for (std::size_t i = 0; i < old_groups * 16; ++i)
            if (detail::occupied_control(ikea::to_u8(old_control.data)[i])) {
                const auto key = load<key_bytes>(ikea::to_u8(old_slots.data) + i * tuple_bytes);
                value_type v{};
                if constexpr (V)
                    std::memcpy(v.data(), ikea::to_u8(old_slots.data) + i * tuple_bytes + key_bytes, V);
                insert_swiss_unchecked(key, v, mix_(key));
            }
    }
    struct swiss_position {
        std::size_t slot;
        bool present;
    };
    // One traversal finds either the existing key or an admissible empty/tomb
    // destination. A tombstone is reusable only after absence is established.
    swiss_position resolve_swiss(word key, const typename mix_type::result& h) const {
        if (!groups_)
            return {absent, false};
        auto group = h.swiss & (groups_ - 1);
        std::size_t step = 0, available = absent;
        for (;;) {
            const auto* control = ikea::to_u8(control_.data) + group * 16;
            auto candidates = equal_controls(control, h.fingerprint());
            while (candidates) {
                const auto lane = std::countr_zero(candidates);
                candidates &= candidates - 1;
                const auto slot = group * 16 + lane;
                if (tuple_field<key_bytes>::read(ikea::to_u8(slots_.data) + slot * tuple_bytes) == key)
                    return {slot, true};
            }
            const auto empties = equal_controls(control, empty);
            if (available == absent) {
                const auto free = empties | equal_controls(control, tombstone);
                if (free)
                    available = group * 16 + std::countr_zero(free);
            }
            if (empties)
                return {available, false};
            group = (group + (++step)) & (groups_ - 1);
        }
    }
    void store_swiss(std::size_t slot, word key, const bytes<V>& v,
                     const typename mix_type::result& h) {
        deleted_ -= ikea::to_u8(control_.data)[slot] == tombstone;
        store<key_bytes>(ikea::to_u8(slots_.data) + slot * tuple_bytes, key);
        if constexpr (V)
            std::memcpy(ikea::to_u8(slots_.data) + slot * tuple_bytes + key_bytes, v.data(), V);
        ikea::to_u8(control_.data)[slot] = h.fingerprint();
        ++spill_size_;
    }
    [[gnu::noinline]] bool insert_slow(word key, const bytes<V>& v,
                                       const typename mix_type::result& h, word free) {
        auto* p = bucket(h.bucket);
        auto found = p[overflow_offset] ? resolve_swiss(key, h) : swiss_position{absent, false};
        if (found.present) {
            if constexpr (V)
                std::memcpy(ikea::to_u8(slots_.data) + found.slot * tuple_bytes + key_bytes, v.data(), V);
            return false;
        }
        if (free)
            occupy(p, unsigned(std::countr_zero(free)), h.quotient, v);
        else {
            if (!groups_ || (spill_size_ + deleted_ + 1) * 8 > groups_ * 16 * 7) {
                rehash_swiss(groups_ ? groups_ * 2 : 1);
                found.slot = absent;
            }
            if (found.slot == absent)
                found = resolve_swiss(key, h);
            store_swiss(found.slot, key, v, h);
            if (p[overflow_offset] != detail::unknown_overflow)
                ++p[overflow_offset];
        }
        ++size_;
        return true;
    }

  public:
    // Geometry, seed, tombstones and sticky overflow counts are unchanged.
    // The caller publishes this independent owner only after private edits.
    table clone(allocation_context* memory = nullptr) const {
        return table(*this, memory);
    }
    explicit table(word seed, allocation_context* memory = nullptr)
        : memory_(memory), primary_(primary_bytes(), memory), mix_{seed & mask<W>} {
        if constexpr (!bitmap)
            for (std::size_t i = 0; i < bucket_count; ++i) {
                if constexpr (E > K)
                    std::memset(bucket(i), 255, T * E);
                std::memset(bucket(i) + flags_offset, 255, T);
            }
        // Validate the actual tile placement, including 96-byte bucket strides.
        if constexpr (!bitmap) {
            using F = sp::layout<E, sp::geometry::local, 0>;
            auto view = sp::view<F>::attach(N, {{{{primary_.data, T * E}, E}, {}, {}}});
            require(bool(view), "SeriesPack bucket admission");
        }
    }
    std::size_t size() const {
        return size_;
    }
    std::size_t spill_size() const {
        return spill_size_;
    }
    std::size_t swiss_groups() const {
        return groups_;
    }
    std::size_t primary_size() const {
        return size_ - spill_size_;
    }
    std::size_t bytes_used() const {
        return primary_.capacity() + control_.capacity() + slots_.capacity() + sizeof(*this);
    }
    storage_usage storage() const {
        auto result = primary_.usage();
        result += control_.usage();
        result += slots_.usage();
        return result;
    }
    void retain_storage(typename Storage::retention& holds) const {
        primary_.retain(holds);
        control_.retain(holds);
        slots_.retain(holds);
    }
    typename Storage::trim_result relinquish_surplus() {
        for (auto* storage : {&primary_, &control_, &slots_}) {
            auto result = storage->relinquish_surplus();
            if (!result)
                return result;
        }
        return {};
    }
    template <class Visit> void allocations(Visit&& visit) const {
        visit("primary", static_cast<const std::byte*>(primary_.data), primary_.size);
        if (control_.size)
            visit("swiss_controls", static_cast<const std::byte*>(control_.data), control_.size);
        if (slots_.size)
            visit("swiss_tuples", static_cast<const std::byte*>(slots_.data), slots_.size);
    }
    /// Current physical plane for owner preservation/generation checks. The
    /// table and backing stay stable from enter through the complete command.
    /// Header bytes include local owner metadata; pointers are not durable ids.
    using plane_type = plane;
    std::span<const std::byte> plane_storage(plane id) const noexcept {
        switch (id) {
        case plane::header: return std::as_bytes(std::span(this, 1));
        case plane::primary: return {primary_.data, primary_.size};
        case plane::swiss_controls: return {control_.data, control_.size};
        case plane::swiss_tuples: return {slots_.data, slots_.size};
        default: return {};
        }
    }
    /// Conservative additional records for one guarded command. Actual record
    /// count can be smaller; context.prepare still admits the complete list.
    static constexpr std::size_t effect_capacity_insert() noexcept { return 4; }
    static constexpr std::size_t effect_capacity_erase() noexcept { return 4; }
    static constexpr std::size_t effect_capacity_modify() noexcept { return (V + 7) / 8 + 4; }
    static constexpr std::size_t effect_capacity_modify_or_insert() noexcept {
        return effect_capacity_modify();
    }
    std::size_t capacity() const {
        return bucket_count * N;
    }
    bool primary_contains(word key) const {
        const auto p = prepare(key);
        return primary_find(ikea::to_u8(p.primary), p.hash.quotient) != N;
    }

  private:
    write_position prepare_write(word key, value_type& previous) {
        require(!(key & ~mask<W>), "prepared key outside W-bit domain");
        const auto h = mix_(key);
        auto* p = bucket(h.bucket);
        auto slot = primary_find(p, h.quotient);
        if (slot != N) {
            consume_primary(p, slot, [&](const auto& value) { previous = value; });
            return {h, slot, true, true};
        }
        if constexpr (!bitmap)
            if (p[overflow_offset]) {
                const auto found = find_swiss(key, h);
                if (found != absent) {
                    consume_tuple(ikea::to_u8(slots_.data) + found * tuple_bytes,
                                  [&](const auto& value) { previous = value; });
                    return {h, found, false, true};
                }
            }
        if constexpr (bitmap)
            slot = unsigned(h.quotient);
        else
            each<T>([&](auto tile) {
                const auto flags = p[flags_offset + tile];
                if (slot == N && flags)
                    slot = tile * 8 + std::countr_zero(unsigned(flags));
            });
        if (slot != N)
            return {h, slot, true, false};
        if (!groups_ || (spill_size_ + deleted_ + 1) * 8 > groups_ * 16 * 7)
            rehash_swiss(groups_ ? groups_ * 2 : 1);
        std::size_t group = h.swiss & (groups_ - 1), step = 0;
        for (;;) {
            const auto* controls = ikea::to_u8(control_.data) + group * 16;
            const auto available =
                equal_controls(controls, empty) | equal_controls(controls, tombstone);
            if (available)
                return {h, group * 16 + std::countr_zero(available), false, false};
            group = (group + (++step)) & (groups_ - 1);
        }
    }
    bool insert_prepared_unchecked(word key, const write_position& position,
                                   const value_type& value) {
        auto* p = bucket(position.hash.bucket);
        const auto slot = position.slot;
        if (position.primary) {
            if (!position.present) {
                if constexpr (!bitmap)
                    set_point<E>(p, slot, N, position.hash.quotient);
                mark(p, slot, true);
            }
            write_value(p, slot, value);
        } else {
            if (!position.present) {
                store<key_bytes>(ikea::to_u8(slots_.data) + slot * tuple_bytes, key);
                deleted_ -= ikea::to_u8(control_.data)[slot] == tombstone;
                ikea::to_u8(control_.data)[slot] = position.hash.fingerprint();
                ++spill_size_;
                if (p[overflow_offset] != detail::unknown_overflow)
                    ++p[overflow_offset];
            }
            if constexpr (V)
                std::memcpy(ikea::to_u8(slots_.data) + slot * tuple_bytes + key_bytes, value.data(), V);
        }
        size_ += !position.present;
        return !position.present;
    }

  public:
    // The caller has already established absence and retains exclusive owner
    // access until insert. This is the exact vacant slot insert will choose.
    location vacancy_for_absent(word key) const {
        const auto p = prepare(key);
        unsigned slot = N;
        if constexpr (bitmap)
            slot = p.hash.quotient;
        else
            each<T>([&](auto tile) {
                const auto flags = ikea::to_u8(p.primary)[flags_offset + tile];
                if (slot == N && flags)
                    slot = tile * 8 + std::countr_zero(unsigned(flags));
            });
        return {p.hash.bucket, slot, slot != N};
    }
    // Private/exclusive owner preflight. A subsequent absent-key insertion
    // cannot allocate if no intervening index mutation consumes this room.
    void prepare_absent(word key) {
        require(!(key & ~mask<W>), "prepared key outside W-bit domain");
        const auto p = prepare(key);
        require(primary_find(ikea::to_u8(p.primary), p.hash.quotient) == N, "prepared key is present");
        if constexpr (!bitmap) {
            require(!ikea::to_u8(p.primary)[overflow_offset] || find_swiss(key, p.hash) == absent,
                    "prepared spill key is present");
            if (!vacancy_for_absent(key).primary &&
                (!groups_ || (spill_size_ + deleted_ + 1) * 8 > groups_ * 16 * 7))
                rehash_swiss(groups_ ? groups_ * 2 : 1);
        }
    }
    [[gnu::always_inline]] probe prepare(word key) const {
        auto h = mix_(key);
        return {h, reinterpret_cast<const std::byte*>(bucket(h.bucket))};
    }
    template <bool Hint = true> [[gnu::always_inline]] void prefetch(const probe& p) const {
        if constexpr (Hint)
            __builtin_prefetch(ikea::to_u8(p.primary), 0, 3);
    }
    /// Materialized result; missing values are zero. The default preserves the
    /// predictable primary-hit branch. Select the hint around the whole consumer.
    template <lookup_hint Hint = lookup_hint::predictable>
    [[gnu::always_inline]] lookup_value get(word key) const {
#if defined(__AVX512VL__) && defined(__AVX512F__) && defined(__BMI__) && defined(__x86_64__)
        if constexpr (Hint == lookup_hint::unpredictable && !bitmap &&
                      (Kernel == search_kernel::generic ||
                       Kernel == search_kernel::exact_small512) && (V == 0 || V == 8)) {
            const auto p = prepare(key);
            // Decide from bucket state before exact matching, so membership
            // does not speculate a Swiss traversal on the no-overflow path.
            unsigned overflow = ikea::to_u8(p.primary)[overflow_offset];
            asm("" : "+r"(overflow));
            if (overflow) [[unlikely]] {
                lookup_value out;
                out.found = find(key, [&](const auto& value) { out.value = value; });
                return out;
            }
            const word matches = primary_matches(ikea::to_u8(p.primary), p.hash.quotient);
            lookup_value out;
            out.found = matches != 0;
            if constexpr (V == 8) {
                static constexpr word zero = 0;
                word address;
                // TZCNT CF means zero mask. LEA/CMOV preserve it. Choose an
                // actual readable zero word on absence: masked-off cold loads
                // were costly, and C++ pointer selection reintroduced a branch.
                asm("tzcntq %2, %0; leaq (%3,%0,8), %0; cmovcq %4, %0"
                    : "=&r"(address), "=@ccae"(out.found)
                    : "r"(matches), "r"(ikea::to_u8(p.primary) + value_offset), "r"(&zero));
                store<8>(out.value.data(), load<8>(reinterpret_cast<const std::uint8_t*>(address)));
            }
            return out;
        }
#endif
        lookup_value out;
        out.found = find(key, [&](const auto& value) { out.value = value; });
        return out;
    }
    /// Read-only bytes, valid while this owner is retained and unmodified.
    /// Absence supplies V readable zero bytes. No alignment or typed-object
    /// lifetime is promised; use byte loads/memcpy. Striped values use get/find.
    template <lookup_hint Hint = lookup_hint::predictable>
    [[gnu::always_inline]] borrowed_value get_borrowed(word key) const
        requires(V > 0 && (V <= 8 || V % 8 == 0))
    {
        const auto p = prepare(key);
        if constexpr (bitmap) {
            const auto row = primary_find(ikea::to_u8(p.primary), p.hash.quotient);
            return row == N ? borrowed_value{} :
                              borrowed_value{reinterpret_cast<const std::byte*>(ikea::to_u8(p.primary) + value_offset + row * V), true};
        } else {
            if constexpr (Hint == lookup_hint::unpredictable) {
                unsigned overflow = ikea::to_u8(p.primary)[overflow_offset];
                asm("" : "+r"(overflow));
                if (!overflow) {
                    const auto matches = primary_matches(ikea::to_u8(p.primary), p.hash.quotient);
                    const auto row = unsigned(std::countr_zero(matches));
                    // An absent integer candidate is never dereferenced. Select
                    // actual initialized zero storage before returning a pointer.
                    std::uintptr_t address = std::uintptr_t(ikea::to_u8(p.primary)) + value_offset + row * V;
#if defined(__x86_64__)
                    asm("testq %1, %1; cmovzq %2, %0" : "+r"(address)
                        : "r"(matches), "r"(missing_value_.data()) : "cc");
#else
                    address = matches ? address : std::uintptr_t(missing_value_.data());
#endif
                    return {reinterpret_cast<const std::byte*>(address), matches != 0};
                }
            }
            const auto row = primary_find(ikea::to_u8(p.primary), p.hash.quotient);
            if (row != N)
                return {reinterpret_cast<const std::byte*>(ikea::to_u8(p.primary) + value_offset + row * V), true};
            if (!ikea::to_u8(p.primary)[overflow_offset])
                return {};
            const auto* value = borrow_overflow(key, p.hash.swiss);
            return value ? borrowed_value{reinterpret_cast<const std::byte*>(value), true} : borrowed_value{};
        }
    }
    template <bool Eager = false, class Consume>
    [[gnu::always_inline]] bool find(word key, Consume&& consume) const {
        const auto p = prepare(key);
        if constexpr (Eager)
            if (groups_)
                __builtin_prefetch(ikea::to_u8(control_.data) + (p.hash.swiss & (groups_ - 1)) * 16, 0, 3);
        return finish(p, key, std::forward<Consume>(consume));
    }
    template <class Consume>
    [[gnu::always_inline]] bool finish(const probe& p, word key, Consume&& consume) const {
#if defined(__aarch64__)
        if constexpr (Kernel == search_kernel::neon64 && E == 64 && N == 8 && V == 0) {
            // Membership-only consumers do not need a row index at all.
            bool found;
            if constexpr (E == K)
                found = p.hash.quotient ? bool(equal64_neon(ikea::to_u8(p.primary), p.hash.quotient))
                                        : bool(primary_matches(ikea::to_u8(p.primary), 0));
            else
                found = bool(equal64_neon(ikea::to_u8(p.primary), p.hash.quotient));
            if (found)
                return true;
            return ikea::to_u8(p.primary)[overflow_offset] && overflow_found(read_overflow(key, p.hash.swiss));
        }
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr (Kernel == search_kernel::fused64 && E == 64 && N == 8 && V == 8) {
            auto values = _mm512_loadu_si512(ikea::to_u8(p.primary) + value_offset);
            asm("" : "+v"(values));
            __mmask8 exact = _mm512_cmpeq_epi64_mask(_mm512_loadu_si512(ikea::to_u8(p.primary)),
                                                     _mm512_set1_epi64(p.hash.quotient));
            if constexpr (E == K)
                if (p.hash.quotient == 0) [[unlikely]]
                    exact &= __mmask8(occupied(ikea::to_u8(p.primary)));
            asm("" : "+k"(exact));
            if (exact) {
                const auto selected = _mm512_maskz_compress_epi64(exact, values);
                value_type value;
                store<8>(value.data(), _mm_cvtsi128_si64(_mm512_castsi512_si128(selected)));
                consume_located(consume, value,
                                {p.hash.bucket, unsigned(std::countr_zero(unsigned(exact))), true});
                return true;
            }
        } else
#endif
        {
            if constexpr (bitmap || Kernel == search_kernel::neon64 ||
                          Kernel == search_kernel::neon30 ||
                          Kernel == search_kernel::body_small_neon) {
                const auto slot = primary_find(ikea::to_u8(p.primary), p.hash.quotient);
                if (slot != N) {
                    consume_primary(
                        ikea::to_u8(p.primary), slot, [&](const auto& value) __attribute__((always_inline)) {
                            consume_located(consume, value, {p.hash.bucket, slot, true});
                        });
                    return true;
                }
            } else {
                const auto matches = primary_matches(ikea::to_u8(p.primary), p.hash.quotient);
                if (matches) {
                    const unsigned slot = std::countr_zero(matches);
                    consume_primary(
                        ikea::to_u8(p.primary), slot, [&](const auto& value) __attribute__((always_inline)) {
                            consume_located(consume, value, {p.hash.bucket, slot, true});
                        });
                    return true;
                }
            }
        }
        if constexpr (bitmap)
            return false;
        else {
            if (!ikea::to_u8(p.primary)[overflow_offset])
                return false;
            const auto result = read_overflow(key, p.hash.swiss);
            if constexpr (V)
                if (overflow_found(result)) {
                    consume_located(consume, result.value, {p.hash.bucket, N, false});
                }
            return overflow_found(result);
        }
    }
    template <staged_values Values = staged_values::dependent, class Emit>
    void prepare_staged(staged_probe& state, word key, Emit&& emit) const {
        const auto issue = [&](const void* address, std::size_t size) {
            emit(static_cast<const std::byte*>(address), size);
        };
        state = {prepare(key), key};
        state.first.primary = std::assume_aligned<bucket_alignment>(state.first.primary);
        issue(state.first.primary, B);
        if constexpr (Values == staged_values::lookahead8 && V == 8 && !bitmap)
            each<value_rows / 8>(
                [&](auto line) { issue(state.first.primary + value_offset + line * 64, 64); });
    }
    // One call advances only to an address-dependent boundary. The caller owns
    // whether to prefetch, its cache tier, and the scheduling/yield operation.
    template <staged_values Values = staged_values::dependent, class Consume, class Emit>
    bool advance(staged_probe& s, bool& found, Consume&& consume, Emit&& emit) const {
        const auto issue = [&](const void* address, std::size_t size) {
            emit(static_cast<const std::byte*>(address), size);
        };
        auto* primary = std::assume_aligned<bucket_alignment>(ikea::to_u8(s.first.primary));
        if (s.phase == staged_phase::primary) {
            const auto slot = primary_find(primary, s.first.hash.quotient);
            if constexpr (!(Values == staged_values::lookahead8 && V == 8 && !bitmap))
                s.slot = slot;
            if (slot != N) {
                if constexpr (V) {
                    if constexpr (Values == staged_values::lookahead8 && V == 8 && !bitmap) {
                        consume_primary(primary, slot, consume);
                        found = true;
                        return true;
                    }
                    if constexpr (V % 8 == 0 || V < 8)
                        issue(primary + value_offset + std::size_t(s.slot) * V, V);
                    else
                        each<(V + 7) / 8>([&](auto c) {
                            constexpr unsigned width = std::min(8u, V - c * 8);
                            issue(primary + value_offset + c * value_rows * 8 + s.slot * width,
                                 width);
                        });
                    s.phase = staged_phase::value;
                    return false;
                } else {
                    found = true;
                    return true;
                }
            }
            if constexpr (bitmap) {
                found = false;
                return true;
            } else if (!primary[overflow_offset]) {
                found = false;
                return true;
            }
            s.group = s.first.hash.swiss & (groups_ - 1);
            s.phase = staged_phase::controls;
            issue(std::assume_aligned<16>(ikea::to_u8(control_.data) + s.group * 16), 16);
            return false;
        }
        if (s.phase == staged_phase::value) {
            consume_primary(primary, s.slot, consume);
            found = true;
            return true;
        }
        if (s.phase == staged_phase::controls) {
            const auto* controls = ikea::to_u8(control_.data) + s.group * 16;
            s.candidates = equal_controls(controls, s.first.hash.fingerprint());
            s.empty_group = equal_controls(controls, empty) != 0;
        } else {
            const auto slot = s.group * 16 + std::countr_zero(s.candidates);
            s.candidates &= s.candidates - 1;
            const auto* tuple = ikea::to_u8(slots_.data) + slot * tuple_bytes;
            if (tuple_key(tuple) == s.key) {
                consume_tuple(tuple, consume);
                found = true;
                return true;
            }
        }
        if (s.candidates) {
            s.phase = staged_phase::tuple;
            issue(ikea::to_u8(slots_.data) + (s.group * 16 + std::countr_zero(s.candidates)) * tuple_bytes,
                 tuple_bytes);
            return false;
        }
        if (s.empty_group) {
            found = false;
            return true;
        }
        s.group = (s.group + (++s.step)) & (groups_ - 1);
        s.phase = staged_phase::controls;
        issue(std::assume_aligned<16>(ikea::to_u8(control_.data) + s.group * 16), 16);
        return false;
    }
    template <write_strategy Flow = write_strategy::mixed>
    [[gnu::always_inline]] bool insert(word key, const value_type& v = {}) {
        if constexpr (bitmap) {
            require(!(key & ~mask<W>), "key outside W-bit domain");
            const auto h = mix_(key);
            auto* p = bucket(h.bucket);
            const auto row = unsigned(h.quotient);
            const bool fresh = !has(p, row);
            write_value(p, row, v);
            mark(p, row, true);
            size_ += fresh;
            return fresh;
        } else {
            require(!(key & ~mask<W>), "key outside W-bit domain");
            const auto h = mix_(key);
            auto* p = bucket(h.bucket);
            if constexpr (Flow != write_strategy::existing) {
                const auto matching = matching_rows(p, h.quotient);
                const auto free = vacancies(p);
                const bool fresh = !matching;
                // An existing primary row wins over every vacancy. An absent key
                // needs a Swiss absence proof whenever this bucket has overflow,
                // even if erasure has since left a primary hole.
                const auto admissible = free & (word{0} - word(p[overflow_offset] == 0));
                const auto candidates = [&] {
                    if constexpr (N <= 32)
                        return select_candidates(fresh, matching, unsigned(admissible));
                    else
                        return matching | (admissible & (word{0} - word(fresh)));
                }();
                if (!candidates) [[unlikely]]
                    return insert_slow(key, v, h, free);
                const auto destination = unsigned(std::countr_zero(candidates));
                if constexpr (Flow == write_strategy::mixed) {
                    // Explicit control: idempotent key/flag stores on replacement
                    // are charged, including residual-plane read/modify/write.
                    occupy(p, destination, h.quotient, v);
                    size_ += fresh;
                } else {
                    if (fresh) {
                        assign_point<E>(p, destination, N, h.quotient);
                        p[flags_offset + destination / 8] &= std::uint8_t(~(1u << (destination % 8)));
                        ++size_;
                    }
                    write_value(p, destination, v);
                }
                return fresh;
            }
            const auto row = write_find(p, h.quotient);
            if (row != N) {
                write_value(p, row, v);
                return false;
            }
            const auto free = vacancies(p);
            if (free && !p[overflow_offset]) {
                occupy(p, unsigned(std::countr_zero(free)), h.quotient, v);
                ++size_;
                return true;
            }
            return insert_slow(key, v, h, free);
        }
    }
    /// Invoke once on a hit, with a temporary full-value snapshot. The updater
    /// must not reenter/invalidate this owner or let the value reference escape.
    /// An updater exception leaves logical contents unchanged.
    template <class Update>
    [[gnu::always_inline]] bool modify(word key, Update&& update)
        requires(V > 0)
    {
        require(!(key & ~mask<W>), "modify key outside W-bit domain");
        const auto h = mix_(key);
        auto* p = bucket(h.bucket);
        const auto row = write_find(p, h.quotient);
        if (row != N) {
            value_type next{};
            consume_primary(p, row, [&](const auto& value) { next = value; });
            std::invoke(std::forward<Update>(update), next);
            write_value(p, row, next);
            return true;
        }
        if constexpr (!bitmap)
            if (p[overflow_offset]) {
                const auto slot = find_swiss(key, h);
                if (slot != absent) {
                    value_type next{};
                    consume_tuple(ikea::to_u8(slots_.data) + slot * tuple_bytes,
                                  [&](const auto& value) { next = value; });
                    std::invoke(std::forward<Update>(update), next);
                    std::memcpy(ikea::to_u8(slots_.data) + slot * tuple_bytes + key_bytes, next.data(), V);
                    return true;
                }
            }
        return false;
    }
    /// Invoke once with (old value,true) or (initial,false); return true for a
    /// new key. Preparation may change capacity before a throwing updater.
    template <class Update>
    [[gnu::always_inline]] bool modify_or_insert(word key, const value_type& initial,
                                                 Update&& update)
        requires(V > 0)
    {
        value_type next = initial; // Snapshot possible input aliases before allocation.
        const auto position = prepare_write(key, next);
        std::invoke(std::forward<Update>(update), next, position.present);
        return insert_prepared_unchecked(key, position, next);
    }
    bool erase(word key) {
        if constexpr (bitmap) {
            require(!(key & ~mask<W>), "erase key outside W-bit domain");
            const auto h = mix_(key);
            auto* p = bucket(h.bucket);
            const auto row = unsigned(h.quotient);
            if (!has(p, row))
                return false;
            mark(p, row, false);
            --size_;
            return true;
        } else {
            require(!(key & ~mask<W>), "key outside W-bit domain");
            const auto h = mix_(key);
            auto* p = bucket(h.bucket);
            const auto row = write_find(p, h.quotient);
            if (row != N) {
                assign_point<E>(p, row, N, E > K ? mask<E> : 0);
                p[flags_offset + row / 8] |= std::uint8_t(1u << (row % 8));
                --size_;
                return true;
            }
            if (p[overflow_offset]) {
                const auto found = resolve_swiss(key, h);
                if (found.present) {
                    ikea::to_u8(control_.data)[found.slot] = tombstone;
                    --spill_size_;
                    ++deleted_;
                    --size_;
                    if (p[overflow_offset] != detail::unknown_overflow)
                        --p[overflow_offset];
                    return true;
                }
            }
            return false;
        }
    }
    template <class Context, class Consume>
    lookup_status guarded_find(word key, Context& context, Consume&& consume) const {
        if ((key & ~mask<W>) || !context.enter(*this, key, access_intent::read))
            return lookup_status::denied;
        return find(key, std::forward<Consume>(consume)) ? lookup_status::found
                                                         : lookup_status::absent;
    }

  private:
    template <write_strategy Flow, class Context>
    mutation_status guarded_mutate(word key, const bytes<V>* incoming, Context& context) {
        if ((key & ~mask<W>) || !context.enter(*this, key, access_intent::write))
            return mutation_status::denied;
        // The input may refer to this owner. Preserve it before preparation or
        // idempotent packed-key writes can change any of its bytes.
        bytes<V> snapshot{};
        if (incoming)
            snapshot = *incoming;
        const auto h = mix_(key);
        auto* p = bucket(h.bucket);
        auto row = [&] {
            if constexpr (bitmap || Flow == write_strategy::existing)
                return write_find(p, h.quotient);
            else {
                const auto mask = matching_rows(p, h.quotient);
                return mask ? unsigned(std::countr_zero(mask)) : N;
            }
        }();
        auto swiss = swiss_position{absent, false};
        if constexpr (!bitmap)
            if (row == N && p[overflow_offset])
                swiss = resolve_swiss(key, h);
        const bool existing = row != N || swiss.present;
        if (!incoming && !existing)
            return mutation_status::absent;
        if constexpr (!V)
            if (incoming && existing && (bitmap || Flow != write_strategy::mixed || row == N))
                return mutation_status::unchanged;
        if (incoming && !existing) {
            if constexpr (bitmap)
                row = unsigned(h.quotient);
            else {
                const auto free = vacancies(p);
                if (free)
                    row = unsigned(std::countr_zero(free));
                if (row == N) {
                    if (!groups_ || (spill_size_ + deleted_ + 1) * 8 > groups_ * 16 * 7)
                        return mutation_status::needs_capacity;
                    if (swiss.slot == absent)
                        swiss = resolve_swiss(key, h);
                }
            }
        }
        std::array<ikea::journal::entry, effect_capacity_insert()> damage{};
        unsigned count = 0;
        auto add = [&](plane part, std::size_t offset, std::size_t bytes) {
            damage[count++] = {this, unsigned(part), offset, bytes};
        };
        if (row != N)
            add(plane::primary, h.bucket * stride, stride);
        else {
            if (!existing || !incoming) {
                add(plane::swiss_controls, swiss.slot, 1);
                if (p[overflow_offset] != detail::unknown_overflow)
                    add(plane::primary,
                        h.bucket * stride + overflow_offset, 1);
            }
            if (incoming)
                add(plane::swiss_tuples,
                    swiss.slot * tuple_bytes + (existing ? key_bytes : 0),
                    existing ? V : tuple_bytes);
        }
        constexpr bool shared = Flow == write_strategy::mixed && !bitmap;
        if (!existing || !incoming || (shared && row != N))
            add(plane::header, 0,
                sizeof(*this));
        const auto writes = std::span<const ikea::journal::entry>(damage.data(), count);
        if (!context.prepare(writes))
            return mutation_status::denied;
        static_assert(ikea::effects<Context, table>);
        for (const auto& d : writes)
            context.before(*this, d.plane, d.offset, d.size);
        // Only owner-proven native stores follow; callbacks cannot invalidate
        // this operation's storage or reenter it under the Context contract.
        if (row != N) {
            if (incoming) {
                if constexpr (bitmap) {
                    write_value(p, row, snapshot);
                    if (!existing) {
                        p[row / 8] |= std::uint8_t(1u << (row % 8));
                        ++size_;
                    }
                } else if constexpr (shared) {
                    occupy(p, row, h.quotient, snapshot);
                    size_ += !existing;
                } else {
                    if (!existing) {
                        assign_point<E>(p, row, N, h.quotient);
                        p[flags_offset + row / 8] &= std::uint8_t(~(1u << (row % 8)));
                        ++size_;
                    }
                    write_value(p, row, snapshot);
                }
            } else {
                if constexpr (bitmap)
                    p[row / 8] &= std::uint8_t(~(1u << (row % 8)));
                else {
                    assign_point<E>(p, row, N, E > K ? mask<E> : 0);
                    p[flags_offset + row / 8] |= std::uint8_t(1u << (row % 8));
                }
                --size_;
            }
        } else if (incoming) {
            if (existing) {
                if constexpr (V)
                    std::memcpy(ikea::to_u8(slots_.data) + swiss.slot * tuple_bytes + key_bytes, snapshot.data(),
                                V);
            } else {
                store_swiss(swiss.slot, key, snapshot, h);
                ++size_;
                if (p[overflow_offset] != detail::unknown_overflow)
                    ++p[overflow_offset];
            }
        } else {
            ikea::to_u8(control_.data)[swiss.slot] = tombstone;
            --spill_size_;
            ++deleted_;
            --size_;
            if (p[overflow_offset] != detail::unknown_overflow)
                --p[overflow_offset];
        }
        return !incoming  ? mutation_status::erased
               : existing ? (V ? mutation_status::updated : mutation_status::unchanged)
                          : mutation_status::inserted;
    }
    template <bool InsertMissing, class Context, class Update>
    mutation_status guarded_modify_impl(word key, const value_type& initial, Context& context,
                                        Update&& update)
        requires(V > 0)
    {
        if ((key & ~mask<W>) || !context.enter(*this, key, access_intent::write))
            return mutation_status::denied;
        value_type next = initial;
        const auto h = mix_(key);
        auto* p = bucket(h.bucket);
        auto row = write_find(p, h.quotient);
        auto swiss = swiss_position{absent, false};
        if constexpr (!bitmap)
            if (row == N && p[overflow_offset])
                swiss = resolve_swiss(key, h);
        const bool existing = row != N || swiss.present;
        if (!existing) {
            if constexpr (!InsertMissing)
                return mutation_status::absent;
            if constexpr (bitmap)
                row = unsigned(h.quotient);
            else {
                const auto free = vacancies(p);
                if (free)
                    row = unsigned(std::countr_zero(free));
                if (row == N) {
                    if (!groups_ || (spill_size_ + deleted_ + 1) * 8 > groups_ * 16 * 7)
                        return mutation_status::needs_capacity;
                    if (swiss.slot == absent)
                        swiss = resolve_swiss(key, h);
                }
            }
        } else if (row != N)
            consume_primary(p, row, [&](const auto& value) { next = value; });
        else
            consume_tuple(ikea::to_u8(slots_.data) + swiss.slot * tuple_bytes,
                          [&](const auto& value) { next = value; });
        std::array<ikea::journal::entry, effect_capacity_modify()> damage{};
        unsigned count = 0;
        auto add = [&](plane part, std::size_t offset, std::size_t bytes) {
            damage[count++] = {this, unsigned(part), offset, bytes};
        };
        if (row != N) {
            if (!existing)
                add(plane::primary, h.bucket * stride, stride);
            else if constexpr (V % 8 == 0)
                add(plane::primary,
                    h.bucket * stride + value_offset + row * V, V);
            else
                each<(V + 7) / 8>([&](auto c) {
                    constexpr unsigned width = std::min(8u, V - c * 8);
                    add(plane::primary,
                        h.bucket * stride + value_offset + std::size_t(c) * value_rows * 8 +
                            row * width,
                        width);
                });
        } else {
            if (!existing) {
                add(plane::swiss_controls, swiss.slot, 1);
                if (p[overflow_offset] != detail::unknown_overflow)
                    add(plane::primary,
                        h.bucket * stride + overflow_offset, 1);
            }
            add(plane::swiss_tuples,
                swiss.slot * tuple_bytes + (existing ? key_bytes : 0), existing ? V : tuple_bytes);
        }
        if (!existing)
            add(plane::header, 0,
                sizeof(*this));
        const auto writes = std::span<const ikea::journal::entry>(damage.data(), count);
        if (!context.prepare(writes))
            return mutation_status::denied;
        // Denied/capacity-refused calls never invoke user code. A throwing updater
        // precedes damage publication and every live store; prepared resources
        // must unwind safely. Effects on other objects remain caller-owned.
        std::invoke(std::forward<Update>(update), next, existing);
        static_assert(ikea::effects<Context, table>);
        for (const auto& d : writes)
            context.before(*this, d.plane, d.offset, d.size);
        if (row != N) {
            if (!existing) {
                if constexpr (!bitmap)
                    assign_point<E>(p, row, N, h.quotient);
                mark(p, row, true);
                ++size_;
            }
            write_value(p, row, next);
        } else if (existing)
            std::memcpy(ikea::to_u8(slots_.data) + swiss.slot * tuple_bytes + key_bytes, next.data(), V);
        else {
            store_swiss(swiss.slot, key, next, h);
            ++size_;
            if (p[overflow_offset] != detail::unknown_overflow)
                ++p[overflow_offset];
        }
        return existing ? mutation_status::updated : mutation_status::inserted;
    }

  public:
    template <class Context, class Update>
    mutation_status guarded_modify(word key, Context& context, Update&& update)
        requires(V > 0)
    {
        return guarded_modify_impl<false>(key, value_type{}, context, [&](value_type& next, bool) {
            std::invoke(std::forward<Update>(update), next);
        });
    }
    template <class Context, class Update>
    mutation_status guarded_modify_or_insert(word key, const value_type& initial, Context& context,
                                             Update&& update)
        requires(V > 0)
    {
        return guarded_modify_impl<true>(key, initial, context, std::forward<Update>(update));
    }
    template <write_strategy Flow = write_strategy::mixed, class Context>
    mutation_status guarded_insert(word key, const value_type& value, Context& context) {
        return guarded_mutate<Flow>(key, &value, context);
    }
    template <class Context> mutation_status guarded_erase(word key, Context& context) {
        return guarded_mutate<write_strategy::existing>(key, nullptr, context);
    }
    template <class Visit> void visit(Visit&& visitor) const {
        for (std::size_t i = 0; i < bucket_count; ++i) {
            const auto* p = bucket(i);
            for (unsigned slot = 0; slot < N; ++slot) {
                bool occupied;
                if constexpr (bitmap)
                    occupied = has(p, slot);
                else
                    occupied = present(p, slot / 8) & (1u << (slot % 8));
                if (!occupied)
                    continue;
                word q;
                if constexpr (bitmap)
                    q = slot;
                else
                    q = point<E>(p, slot, N);
                value_type v{};
                consume_primary(p, slot, [&](const auto& x) { v = x; });
                visitor(mix_.recover(i, q), v);
            }
        }
        for (std::size_t i = 0; i < groups_ * 16; ++i)
            if (detail::occupied_control(ikea::to_u8(control_.data)[i])) {
                value_type v{};
                if constexpr (V)
                    std::memcpy(v.data(), ikea::to_u8(slots_.data) + i * tuple_bytes + key_bytes, V);
                visitor(load<key_bytes>(ikea::to_u8(slots_.data) + i * tuple_bytes), v);
            }
    }
    void validate() const {
        std::size_t count = 0;
        std::vector<std::size_t> spills(bucket_count);
        for (std::size_t i = 0; i < groups_ * 16; ++i)
            if (detail::occupied_control(ikea::to_u8(control_.data)[i]))
                ++spills[mix_(load<key_bytes>(ikea::to_u8(slots_.data) + i * tuple_bytes)).bucket];
        if constexpr (!bitmap)
            for (std::size_t i = 0; i < bucket_count; ++i)
                require(bucket(i)[overflow_offset] == detail::unknown_overflow ||
                            bucket(i)[overflow_offset] == spills[i],
                        "exact overflow count");
        if constexpr (B == 64 && T == 2 && E == 30)
            for (std::size_t i = 0; i < bucket_count; ++i)
                require(bucket(i)[62] == 0, "reserved zero shuffle byte");
        visit([&](word key, const auto&) {
            ++count;
            require(find(key, [](auto) {}), "iteration reachable");
        });
        require(count == size_, "owner population");
    }
};
} // namespace ikea::hashtable
