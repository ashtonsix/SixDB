#include <ikea/tuplepack/packet_layout.h>

namespace ikea::tuplepack {
template <unsigned N>
std::expected<packet_layout<N>, error> packet_layout<N>::make(unsigned rows, unsigned map_slots,
                                                              std::span<const unsigned> groups) {
    if (!std::has_single_bit(rows) || rows > N || map_slots > N / rows)
        return std::unexpected(error::map);
    packet_layout p;
    p.rows_ = rows;
    p.slots_ = map_slots;
    p.owners_.fill(255);
    unsigned begin = 0;
    const auto append = [&](unsigned length) {
        if (!length || length > map_slots - begin)
            return false;
        for (unsigned r = 0; r < rows; ++r)
            for (unsigned i = 0; i < length; ++i) {
                const auto out = rows * begin + r * length + i;
                p.offsets_[r * (N / rows) + begin + i] = byte(out);
                p.owners_[out] = byte(r);
                p.row_major_ &= out == r * (N / rows) + begin + i;
            }
        begin += length;
        return true;
    };
    if (groups.empty()) {
        if (map_slots)
            append(map_slots);
    } else {
        p.single_group_ = groups.size() == 1;
        for (auto length : groups)
            if (!append(length))
                return std::unexpected(error::map);
    }
    if (begin != map_slots)
        return std::unexpected(error::map);
    if (rows == 1)
        p.single_group_ = true;
    return p;
}

template class packet_layout<8>;
template class packet_layout<64>;
} // namespace ikea::tuplepack
