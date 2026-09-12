#include "composition.h"
#include "byte_route.h"

namespace tuple_composition_probe {
recipe prepare(bool reordered, bool partial) {
    std::array<code,128> codes;
    for (unsigned i = 0; i < 64; ++i) {
        const auto w = byte(width(i));
        const auto offset = byte(reordered ? (17 * i + 3) % 64 : i);
        codes[2 * i] = {offset, byte(reordered ? 8 - w : 0), w};
        codes[2 * i + 1] = {offset, byte(reordered ? 0 : w), byte(8 - w)};
    }
    auto result = prepare_codes(codes,partial);
    require(bool(result), "fixture physical schema");
    result->reordered = reordered;
    return std::move(*result);
}
std::expected<recipe,error> prepare_codes(const std::array<code,128>& codes, bool partial) {
    recipe result;
    result.partial = partial; result.codes = codes;
    std::array<code, 128> semantic;
    for (unsigned i = 0; i < 64; ++i) {
        const auto w = byte(width(i));
        if (codes[2 * i].width != w || codes[2 * i + 1].width != 8 - w) return std::unexpected(error::schema);
        semantic[2 * i] = {byte(i), 0, w};
        semantic[2 * i + 1] = {byte(i), w, byte(8 - w)};
        result.maps[0][i] = 2 * i;
        result.maps[1][i] = 2 * i + 1;
    }
    if (!prepare_read({64,codes},result.maps[0])) return std::unexpected(error::schema);
    for (unsigned p = 0; p < 2; ++p) {
        result.read[p] = *prepare_read({64, result.codes}, result.maps[p]);
        auto write_map = result.maps[p];
        if (partial && p == 1)
            for (unsigned i = 0; i < 64; ++i) if (i % 3) write_map[i] = 255;
        result.write[p] = *prepare_write({64, result.codes}, write_map);
        result.assembly[p] = prepare_write({64, semantic}, result.maps[p])->rounds[0];
    }
    const auto decoded_bits = unite(
        compose(routes(result.assembly[0]), routes(result.read[0].operation)),
        compose(routes(result.assembly[1]), routes(result.read[1].operation)));
    result.decoded = lower(decoded_bits);
    auto normalized = recognize_bytes(decoded_bits);
    if (normalized.supported) result.byte_decoder = std::make_shared<byte_route>(std::move(normalized));
    return result;
}

TUPLE_CC std::uint64_t replace_and_sum_delta(const recipe& p, raw_view view,
                                            native_packet low, native_packet high) {
    // Both reads share the old payload load. The summary consumes assembled
    // uint16 values; these semantics belong here, outside the code primitive.
    const auto old = load(view);
    const auto old_low = transform<false>(old, p.read[0].operation);
    const auto old_high = transform<false>(old, p.read[1].operation);
    const auto old_values = either(transform<true>(old_low, p.assembly[0]),
                                   transform<true>(old_high, p.assembly[1]));
    const auto old_sum = sum_native(old_values);
    // Both packets are admitted before entry. Merge their disjoint bit writes
    // before issuing one group of stores; no intermediate version is exposed
    // to another packet or to a summary consumer.
    // The union preserves exactly those bits preserved by BOTH writers. This
    // also covers partial unions, where OR-ing the two inputs alone is wrong.
    const auto preserve = both(load_packet(p.write[0].preserve.data()),
                               load_packet(p.write[1].preserve.data()));
    const auto updated = either(both(old, preserve),
        either(transform<true>(low, p.write[0].rounds[0]), transform<true>(high, p.write[1].rounds[0])));
    // The summary is over actual post-merge values, including untouched bits.
    // This intentionally direct composition is a semantic control. The
    // performance experiment must compare simplifying the composed transform.
    const auto new_low = transform<false>(updated, p.read[0].operation);
    const auto new_high = transform<false>(updated, p.read[1].operation);
    const auto new_values = either(transform<true>(new_low, p.assembly[0]),
                                   transform<true>(new_high, p.assembly[1]));
    const auto delta = sum_native(new_values) - old_sum;
    store(view, updated);
    return delta;
}
}
