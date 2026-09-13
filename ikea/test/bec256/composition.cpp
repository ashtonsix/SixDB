#include <ikea/bec256.h>
#include <ikea/bec256/author/chain.h>
#include <ikea/bec256/author/write.h>
#include "reference.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <cstring>

namespace bc = ikea::bec256;
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
using chain = bc::chain<8>;
struct context {
    const bc::source *a, *b;
    std::array<bc::byte, 64> mask, output;
    unsigned calls = 0;
};
chain::result read(void *p, std::size_t, std::uint64_t active, bc::pipeline_bits, unsigned) {
    auto &c = *static_cast<context *>(p);
    if (!active)
        return {{}, 0, true};
    ++c.calls;
    return {bc::pipeline_bits::from(bc::native::read_pair(*c.a, *c.b)), active, false};
}
chain::result filter(void *p, std::size_t, std::uint64_t active, bc::pipeline_bits v, unsigned) {
    auto &c = *static_cast<context *>(p);
    ++c.calls;
    return {bc::pipeline_bits::from(
                bc::native::intersection(v.get(), bc::native::load_pair(c.mask.data()))),
            active, false};
}
void done(void *p, std::size_t, std::uint64_t active, bc::pipeline_bits v) {
    auto &c = *static_cast<context *>(p);
    if (active)
        bc::native::store_pair(c.output.data(), v.get());
}
#endif
int main() {
    std::mt19937_64 rng(0xbec2512);
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    auto pipeline =
        chain::prepare(std::array<chain::function, 2>{chain::stage<read>, chain::stage<filter>},
                       chain::completion<done>);
    assert(pipeline);
    context stopped{nullptr, nullptr, {}, {}};
    stopped.output.fill(bc::byte{0xa5});
    pipeline->run(&stopped, 0, 0, {});
    assert(stopped.calls == 0 && stopped.output[0] == bc::byte{0xa5});
#endif
    for (unsigned r = 0; r < 1024; ++r) {
        bc::plain_block a, b;
        for (auto &v : a)
            v = bc::byte(rng());
        for (auto &v : b)
            v = bc::byte(rng());
        if (r % 7 == 0)
            a.fill(bc::byte{0});
        if (r % 11 == 0)
            b.fill(bc::byte{255});
        const auto pa = bec_reference::population(a.data()),
                   pb = bec_reference::population(b.data());
        auto ea = bc::prepare(a, pa), eb = bc::prepare(b, pb);
        assert(ea && eb);
        // Independent owners in one case, a dense succession in the other.
        for (bool dense : {false, true}) {
            std::array<bc::byte, 192> memory{};
            unsigned offset = dense ? ea->bytes() : 96;
            bc::destination dest{memory};
            std::array<ikea::owner_write, 4> entries;
            ikea::source_write_journal journal{entries};
            assert(ea->write(dest, 0, journal) && eb->write(dest, offset, journal));
            auto sa = bc::source::admit(std::span(memory).first(r % 2 ? ea->bytes() : 64),
                                        ea->bytes(), pa);
            auto sb = bc::source::admit(std::span(memory).subspan(offset, r % 2 ? eb->bytes() : 64),
                                        eb->bytes(), pb);
            assert(sa && sb);
            std::array<bc::byte, 64> output;
            bc::decode_pair(*sa, *sb, output);
            assert(std::equal(a.begin(), a.end(), output.begin()) &&
                   std::equal(b.begin(), b.end(), output.begin() + 32));
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
            context c{&*sa, &*sb, {}, {}};
            for (auto &v : c.mask)
                v = bc::byte(rng());
            auto inline_value = filter(&c, 0, 3, read(&c, 0, 3, {}, 0).values, 1).values;
            std::array<bc::byte, 64> expected;
            bc::native::store_pair(expected.data(), inline_value.get());
            pipeline->run(&c, 0, 3, {});
            assert(c.output == expected);
            for (unsigned i = 0; i < 64; ++i)
                assert(expected[i] == (output[i] & c.mask[i]));
            // Encoding starts with a native 512-bit bitset, and carries two
            // independent population/size associations without embedded headers.
            std::array<bc::byte, 128> encoded{};
            auto sizes = bc::native::encode_pair_unchecked(bc::native::read_pair(*sa, *sb), pa,
                                                           encoded.data(), pb, encoded.data() + 64);
            assert(sizes.first == ea->bytes() && sizes.second == eb->bytes());
            assert(std::equal(ea->body().begin(), ea->body().end(), encoded.begin()));
            assert(std::equal(eb->body().begin(), eb->body().end(), encoded.begin() + 64));
            encoded.fill(bc::byte{0xa5});
            bc::destination replacement{encoded};
            journal.used = 0;
            sizes = bc::native::encode_pair_exact_unchecked(
                bc::native::read_pair(*sa, *sb), pa, replacement, 0, pb, replacement, 64, journal);
            assert(sizes.first == ea->bytes() && sizes.second == eb->bytes());
            assert(std::equal(ea->body().begin(), ea->body().end(), encoded.begin()));
            assert(std::equal(eb->body().begin(), eb->body().end(), encoded.begin() + 64));
            for (unsigned i = sizes.first; i < 64; ++i)
                assert(encoded[i] == bc::byte{0xa5});
            for (unsigned i = 64 + sizes.second; i < 128; ++i)
                assert(encoded[i] == bc::byte{0xa5});
            assert(journal.used == unsigned(sizes.first != 0) + unsigned(sizes.second != 0));
            for (auto event : journal.entries())
                assert(event.source == &replacement);
#endif
            // Read both inputs before writing overlapping output.
            bc::decode_pair(*sa, *sb, std::span<bc::byte, 64>(memory.data(), 64));
            assert(std::equal(output.begin(), output.end(), memory.begin()));
        }
    }
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    std::puts("Bec256 composition: 2048 dense/scattered pairs, native encoding and inline/CPS "
              "equivalence passed");
#else
    std::puts("Bec256 composition: 2048 dense/scattered ordinary pairs passed; "
              "native/CPS sections unavailable in this profile");
#endif
}
