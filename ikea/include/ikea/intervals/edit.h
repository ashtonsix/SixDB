#pragma once
#include <ikea/intervals/sparse.h>
#include <ikea/effects.h>
#include <cstring>

namespace ikea::intervals {
enum class plane : unsigned { fields, checkpoints, assigned, extent };
/// Borrowed mutable Sizes source, with no portable format or bitmap. Placement
/// and disjointness of all spans, controls, this object and effects are owner
/// proofs. Keep this named source stable until effects have been resolved. Hooks
/// cannot fail, allocate, suspend or mutate admitted inputs. Publication is external.
/// Planes: fields=0, checkpoints=1, assigned=2, extent=3. Every issued store is
/// reported before execution. Tail writes require exclusive authority too.
template <unsigned G, class W, std::unsigned_integral P = std::uint32_t>
struct size_editor {
    static_assert(G >= 16 && G <= 256 && G % 16 == 0);
    static_assert(std::same_as<W,std::uint8_t> || std::same_as<W,std::uint16_t>);
    std::span<W> fields;
    std::span<P> checkpoints;
    unsigned &assigned;
    P &limit;
    P origin, extent_limit;
    unsigned maximum;

    using plane_type = plane;
    /// Resolve issued plane bytes while this named editor and its backing live.
    std::span<const std::byte> plane_storage(plane p) const noexcept {
        switch (p) {
        case plane::fields: return std::as_bytes(fields);
        case plane::checkpoints: return std::as_bytes(checkpoints);
        case plane::assigned: return std::as_bytes(std::span{&assigned, 1});
        case plane::extent: return std::as_bytes(std::span{&limit, 1});
        }
        return {};
    }
    auto source() const {
        return sparse_sizes<G, W, P>{fields.data(), checkpoints.data(), assigned,
                                     origin, limit, maximum};
    }
    unsigned capacity() const noexcept {
        return unsigned(std::min<std::size_t>(fields.size() / 16 * 16, maximum_count));
    }
    std::expected<void, error> validate() const {
        if (assigned > capacity() || checkpoints.size() < (capacity() / G + (capacity() % G != 0)) ||
            maximum > std::numeric_limits<W>::max() || origin > extent_limit)
            return std::unexpected(error::capacity);
        P position = origin;
        for (unsigned rank = 0; rank < assigned; ++rank) {
            if (rank % G == 0 && checkpoints[rank / G] != position)
                return std::unexpected(error::contents);
            if (fields[rank] > maximum || fields[rank] > extent_limit - position)
                return std::unexpected(error::contents);
            position += fields[rank];
        }
        if (position != limit) return std::unexpected(error::contents);
        const unsigned tail = assigned % 16 ? 16 - assigned % 16 : 0;
        for (unsigned i = 0; i < tail; ++i)
            if (fields[assigned + i]) return std::unexpected(error::contents);
        return {};
    }
    unsigned effect_capacity_append() const noexcept {
        return 3 + (assigned % G == 0) + (assigned % 16 == 0);
    }
    /// Conservative additional records; an unchanged value can emit none.
    std::expected<unsigned, error> effect_capacity_resize(unsigned rank) const noexcept {
        if (rank >= assigned) return std::unexpected(error::range);
        const unsigned groups = assigned / G + (assigned % G != 0);
        return 2u + (rank / G + 1 < groups);
    }
    template <class Effects> requires std::same_as<Effects, ikea::journal>
    std::expected<void, error> append(unsigned value, Effects &effects) const {
        if (assigned >= capacity()) return std::unexpected(error::capacity);
        if (value > maximum || value > extent_limit - limit)
            return std::unexpected(error::value);
        const unsigned rank = assigned;
        const bool group = rank % G == 0, native = rank % 16 == 0;
        const unsigned required = 3 + group + native;
        if (effects.used > effects.storage.size() || effects.remaining() < required) return std::unexpected(error::effects);
        if (native) {
            effects.before(*this, 0, std::size_t(rank) * sizeof(W), 16 * sizeof(W));
            std::memset(fields.data() + rank, 0, 16 * sizeof(W));
        }
        if (group) {
            effects.before(*this, 1, std::size_t(rank / G) * sizeof(P), sizeof(P));
            checkpoints[rank / G] = limit;
        }
        effects.before(*this, 0, std::size_t(rank) * sizeof(W), sizeof(W));
        fields[rank] = W(value);
        effects.before(*this, 3, 0, sizeof(P));
        limit += value;
        effects.before(*this, 2, 0, sizeof(unsigned));
        assigned = rank + 1;
        return {};
    }
    /// Replace one size, translating all later endpoints. This differs from
    /// moving one boundary while preserving subsequent endpoints.
    template <class Effects> requires std::same_as<Effects, ikea::journal>
    std::expected<void, error> resize(unsigned rank, unsigned value, Effects &effects) const {
        if (rank >= assigned) return std::unexpected(error::range);
        if (value > maximum) return std::unexpected(error::value);
        const unsigned old = fields[rank];
        if (old == value) return {};
        const bool grow = value > old;
        const unsigned amount = grow ? value - old : old - value;
        if (grow && amount > extent_limit - limit) return std::unexpected(error::value);
        const unsigned first = rank / G + 1;
        const unsigned groups = assigned / G + (assigned % G != 0);
        if (effects.used > effects.storage.size() || effects.remaining() < 2u + (first < groups))
            return std::unexpected(error::effects);
        effects.before(*this, 0, std::size_t(rank) * sizeof(W), sizeof(W));
        fields[rank] = W(value);
        if (first < groups) {
            effects.before(*this, 1, std::size_t(first) * sizeof(P),
                                   std::size_t(groups - first) * sizeof(P));
            for (unsigned g = first; g < groups; ++g)
                checkpoints[g] = grow ? checkpoints[g] + amount : checkpoints[g] - amount;
        }
        effects.before(*this, 3, 0, sizeof(P));
        limit = grow ? limit + amount : limit - amount;
        return {};
    }
};
} // namespace ikea::intervals
