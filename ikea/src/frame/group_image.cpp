#include <ikea/frame/detail/group_image.h>

#include <ikea/seriespack/reader.h>
#include <ikea/seriespack/presets.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>
#include <optional>

namespace ikea::frame::detail {
namespace {
namespace sp = ikea::seriespack;
struct cursor {
    std::span<const std::byte> bytes;
    std::size_t at{};
    bool ok = true;
    template<class U> U take() {
        if (!ok || sizeof(U) > bytes.size() - at) { ok = false; return 0; }
        U x = 0;
        for (unsigned i = 0; i < sizeof(U); ++i)
            x |= U(std::to_integer<unsigned>(bytes[at++])) << (8 * i);
        return x;
    }
    bool skip(std::size_t n) {
        if (!ok || n > bytes.size() - at) return ok = false;
        at += n;
        return true;
    }
};
sp::geometry geometry(image_preset p, unsigned width) {
    return packing_geometry(p, width) == pack_geometry::striped ? sp::geometry::striped : sp::geometry::local;
}
} // namespace

std::expected<group_manifest, image_error> validate_group(std::span<const std::byte> bytes,
    image_preset p, bool bounded, std::size_t part_rows, unsigned group) {
    if (p != image_preset::compact && p != image_preset::bulk_x86 && p != image_preset::bulk_arm)
        return std::unexpected(image_error::descriptor);
    if (part_rows == 0 || part_rows > 65536 || group >= (part_rows + 4095) / 4096)
        return std::unexpected(image_error::geometry);
    cursor c{bytes};
    group_manifest m;
    const auto first = std::size_t{group} * 4096;
    m.blocks = static_cast<unsigned>((std::min(std::size_t{4096}, part_rows - first) + 255) / 256);
    for (unsigned b = 0; b < m.blocks; ++b)
        m.rows[b] = static_cast<std::uint16_t>(std::min(std::size_t{256}, part_rows - first - b * 256));
    if (bounded) {
        unsigned end = 0;
        for (unsigned b = 0; b < m.blocks; ++b) {
            m.lo[b] = c.take<std::uint64_t>();
            m.width[b] = c.take<std::uint8_t>();
            m.start[b] = c.take<std::uint16_t>();
            m.count[b] = c.take<std::uint16_t>();
            if (!c.ok) return std::unexpected(image_error::truncated);
            if (m.width[b] > 64 || m.start[b] != end || m.count[b] > m.rows[b])
                return std::unexpected(image_error::descriptor);
            end += m.count[b];
        }
        m.capacity = c.take<std::uint16_t>(); m.used = c.take<std::uint16_t>();
        if (!c.ok) return std::unexpected(image_error::truncated);
        if (m.used != end || m.used > m.capacity)
            return std::unexpected(image_error::membership);
        m.positions = c.at;
        if (!c.skip(m.capacity)) return std::unexpected(image_error::truncated);
        m.values = c.at;
        if (!c.skip(std::size_t{m.capacity} * 8)) return std::unexpected(image_error::truncated);
        for (unsigned b = 0; b < m.blocks; ++b) {
            int last = -1;
            for (unsigned e = m.start[b]; e < m.start[b] + m.count[b]; ++e) {
                const auto r = std::to_integer<unsigned>(bytes[m.positions + e]);
                if (r >= m.rows[b] || int(r) <= last) return std::unexpected(image_error::membership);
                last = static_cast<int>(r);
            }
        }
    }
    m.leaf_offset = c.at;
    std::array<unsigned, 65> expected_rows{}, next_slot{};
    std::array<bool, 65> present{};
    for (unsigned b = 0; b < m.blocks; ++b) {
        const auto k = c.take<std::uint8_t>(), slot = c.take<std::uint8_t>();
        const auto n = c.take<std::uint16_t>();
        if (!c.ok) return std::unexpected(image_error::truncated);
        if (k > 64 || n != m.rows[b] || (bounded && k != m.width[b]))
            return std::unexpected(image_error::descriptor);
        // Width zero owns no array, so every constant block has slot zero.
        if (slot != (k ? next_slot[k]++ : 0)) return std::unexpected(image_error::membership);
        m.width[b] = k; m.slot[b] = slot;
        expected_rows[k] += n; present[k] = true;
    }
    const auto n_arrays = c.take<std::uint8_t>();
    if (!c.ok) return std::unexpected(image_error::truncated);
    if (n_arrays != std::count(present.begin(), present.end(), true)) return std::unexpected(image_error::membership);
    int previous = -1;
    for (unsigned i = 0; i < n_arrays; ++i) {
        const auto k = c.take<std::uint8_t>();
        const auto rows = c.take<std::uint32_t>(), size = c.take<std::uint32_t>();
        if (!c.ok) return std::unexpected(image_error::truncated);
        if (k > 64 || !present[k] || int(k) <= previous) return std::unexpected(image_error::membership);
        previous = k;
        std::size_t need = 0;
        if (k) {
            const auto g = geometry(p, k);
            const auto required = sp::layout_value{k, 0, g}.required_storage(rows);
            if (!required) return std::unexpected(image_error::leaf);
            need = required->plane_bytes[0];
        }
        if ((k ? rows != expected_rows[k] : rows != 0) || need != size)
            return std::unexpected(image_error::leaf);
        m.arrays[k] = {c.at, size, rows};
        if (!c.skip(size)) return std::unexpected(image_error::truncated);
    }
    if (c.at != bytes.size()) return std::unexpected(image_error::trailing);
    // Parent SUM and point laws assume every exception's child field is zero
    // and every non-exception base+field is representable. Check with Ikea's
    // actual compiled reader, not a second implementation of its byte law.
    if (bounded) {
        std::array<std::optional<sp::erased_reader<std::uint64_t>>, 65> readers;
        for (unsigned k = 1; k <= 64; ++k) if (present[k]) {
            const auto &a = m.arrays[k];
            auto reader = sp::bind_reader<std::uint64_t>({k, geometry(p, k), bytes.subspan(a.offset, a.size), a.rows});
            if (!reader) return std::unexpected(image_error::leaf);
            readers[k].emplace(*reader);
        }
        // A width-k field is below 2^k, so when lo + 2^k - 1 is representable
        // every inlier is, and only the exception fields need reading. Width
        // 0 fields are all zero. Otherwise the block's actual fields decide.
        alignas(64) std::uint64_t fields[256];
        for (unsigned b = 0; b < m.blocks; ++b) {
            unsigned e = m.start[b], end = e + m.count[b];
            const unsigned k = m.width[b];
            if (!k) continue;
            const std::size_t base = std::size_t{m.slot[b]} * 256;
            const auto top = k == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << k) - 1;
            if (top <= std::numeric_limits<std::uint64_t>::max() - m.lo[b]) {
                for (; e < end; ++e)
                    if (readers[k]->get_unchecked(base + std::to_integer<unsigned>(bytes[m.positions + e])) != 0)
                        return std::unexpected(image_error::contents);
                continue;
            }
            readers[k]->read_unchecked(base, m.rows[b], fields);
            for (unsigned r = 0; r < m.rows[b]; ++r) {
                const auto field = fields[r];
                const bool exceptional = e < end && std::to_integer<unsigned>(bytes[m.positions + e]) == r;
                if (exceptional) {
                    if (field != 0) return std::unexpected(image_error::contents);
                    ++e;
                } else if (field > std::numeric_limits<std::uint64_t>::max() - m.lo[b]) {
                    return std::unexpected(image_error::contents);
                }
            }
        }
    }
    return m;
}
} // namespace ikea::frame::detail
