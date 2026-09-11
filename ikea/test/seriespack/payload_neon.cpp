#include <ikea/seriespack/native_neon.h>

#if defined(__aarch64__)

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace sp = ikea::seriespack;
std::uint64_t cases = 0;
unsigned families = 0;
std::uint64_t random_state = 0xb1a26d9b965f740c;

std::uint64_t random_value() {
    random_state ^= random_state >> 12;
    random_state ^= random_state << 25;
    random_state ^= random_state >> 27;
    return random_state * 0x2545f4914f6cdd1d;
}

[[noreturn]] void fail(const char* operation, unsigned w, sp::geometry g,
                       unsigned l, std::size_t position) {
    std::fprintf(stderr, "%s: W=%u geometry=%s LaneBytes=%u position=%zu case=%llu\n",
                 operation, w, g == sp::geometry::local8 ? "local8" : "striped", l,
                 position, static_cast<unsigned long long>(cases));
    std::abort();
}

template<unsigned W>
constexpr std::uint64_t width_mask = [] {
    if constexpr (W == 64) return ~std::uint64_t(0);
    else return (std::uint64_t(1) << W) - 1;
}();

class guarded_page {
public:
    guarded_page() : size_(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))) {
        mapping_ = static_cast<std::uint8_t*>(::mmap(
            nullptr, 3 * size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (mapping_ == MAP_FAILED || ::mprotect(mapping_ + size_, size_, PROT_READ | PROT_WRITE)) {
            std::perror("guarded_page");
            std::abort();
        }
    }
    ~guarded_page() { ::munmap(mapping_, 3 * size_); }
    std::uint8_t* begin() const { return mapping_ + size_; }
    std::uint8_t* end() const { return mapping_ + 2 * size_; }
private:
    std::size_t size_;
    std::uint8_t* mapping_;
};

template<unsigned W, sp::geometry G, class UInt>
[[gnu::noinline]]
void check_decoded(const std::uint8_t* tile, UInt* out, const UInt* expected) {
    sp::neon::decode_tile<W, G>(tile, out);
    for (unsigned i = 0; i != sp::payload_layout<W, G>::tile_values; ++i)
        if (out[i] != expected[i]) fail("decode", W, G, sizeof(UInt), i);
    ++cases;
}

template<unsigned W, sp::geometry G, class UInt>
[[gnu::noinline]]
void check_encoded(const UInt* values, std::uint8_t* out, const std::uint8_t* expected) {
    sp::neon::encode_tile<W, G>(values, out);
    for (unsigned i = 0; i != sp::payload_layout<W, G>::tile_bytes; ++i)
        if (out[i] != expected[i]) fail("encode", W, G, sizeof(UInt), i);
    ++cases;
}

template<unsigned W, sp::geometry G, class UInt>
[[gnu::noinline]]
void check_fragments(const std::uint8_t* tile, const UInt* expected) {
    using F = sp::neon::fragment_traits<W, G, sizeof(UInt)>;
    sp::detail::static_for<sp::payload_layout<W, G>::tile_values / F::lanes>([&](auto part) {
        constexpr unsigned begin = part * F::lanes;
        std::array<UInt, 16 / sizeof(UInt)> decoded{};
        const auto x = sp::neon::read_fragment<W, G, sizeof(UInt), begin>(tile);
        vst1q_u8(reinterpret_cast<std::uint8_t*>(decoded.data()), x);
        for (unsigned lane = 0; lane != decoded.size(); ++lane) {
            const UInt wanted = lane < F::lanes ? expected[begin + lane] : UInt(0);
            if (decoded[lane] != wanted) fail("fragment", W, G, sizeof(UInt), begin + lane);
        }
        ++cases;
    });
}

template<unsigned W, sp::geometry G, class UInt>
[[gnu::noinline]]
void check_family() {
    constexpr unsigned T = sp::payload_layout<W, G>::tile_values;
    constexpr unsigned B = sp::payload_layout<W, G>::tile_bytes;
    constexpr unsigned ScalarBytes = T * sizeof(UInt);
    std::array<UInt, T> values{}, decoded{};
    std::array<std::uint8_t, B> encoded{}, expected{};

    // The separately verified scalar wire codec is the native implementation's
    // oracle. It uses scalar values and bit positions, no NEON field shuffles.
    // Comparing encoded bytes as well as values avoids a self-consistent native
    // encode/decode permutation passing a round-trip-only test.
    const auto exercise = [&] {
        sp::detail::encode_tile<W, G>(values.data(), expected.data());
        check_encoded<W, G>(values.data(), encoded.data(), expected.data());
        check_decoded<W, G>(encoded.data(), decoded.data(), values.data());
    };
    exercise();
    values.fill(static_cast<UInt>(width_mask<W>));
    exercise();
    values.fill(0);
    for (unsigned i = 0; i != T; ++i) {
        for (unsigned bit = 0; bit != W; ++bit) {
            values[i] = static_cast<UInt>(std::uint64_t(1) << bit);
            exercise();
        }
        values[i] = 0;
    }
    for (unsigned trial = 0; trial != 32; ++trial) {
        for (auto& value : values) value = static_cast<UInt>(random_value() & width_mask<W>);
        exercise();
    }
    check_fragments<W, G>(encoded.data(), values.data());

    for (unsigned offset = 0; offset != 64; ++offset) {
        std::array<std::uint8_t, B + 192> storage;
        storage.fill(0xa5);
        auto* p = storage.data() + 64 + offset;
        check_encoded<W, G>(values.data(), p, expected.data());
        check_decoded<W, G>(p, decoded.data(), values.data());
        for (unsigned i = 0; i != storage.size(); ++i)
            if ((i < 64 + offset || i >= 64 + offset + B) && storage[i] != 0xa5)
                fail("write footprint", W, G, sizeof(UInt), i);
    }

    auto exact = std::make_unique<std::uint8_t[]>(B);
    auto exact_values = std::make_unique<UInt[]>(T);
    auto exact_decoded = std::make_unique<UInt[]>(T);
    std::copy(values.begin(), values.end(), exact_values.get());
    check_encoded<W, G>(exact_values.get(), exact.get(), expected.data());
    check_decoded<W, G>(exact.get(), exact_decoded.get(), values.data());
    check_fragments<W, G>(exact.get(), values.data());

    guarded_page tile_page, value_page;
    for (auto* p : {tile_page.begin(), tile_page.end() - B}) {
        check_encoded<W, G>(values.data(), p, expected.data());
        check_decoded<W, G>(p, decoded.data(), values.data());
        check_fragments<W, G>(p, values.data());
    }
    for (auto* p : {value_page.begin(), value_page.end() - ScalarBytes}) {
        std::memcpy(p, values.data(), ScalarBytes);
        check_encoded<W, G>(reinterpret_cast<const UInt*>(p), encoded.data(), expected.data());
        check_decoded<W, G>(encoded.data(), reinterpret_cast<UInt*>(p), values.data());
    }
    if constexpr (W == 0) {
        // Head-only payloads have no pointer obligation at all.
        sp::neon::encode_tile<W, G>(static_cast<const UInt*>(nullptr), nullptr);
        check_decoded<W, G>(nullptr, decoded.data(), values.data());
        check_fragments<W, G>(nullptr, values.data());
    }
    // Explicit low-bit projection accepts values carrying a higher head. This
    // differs from passing those values to the width-valid encode_tile wrapper.
    if constexpr (W < sizeof(UInt) * 8) {
        std::array<UInt, T> full{}, low{};
        for (unsigned trial = 0; trial != 16; ++trial) {
            for (unsigned i = 0; i != T; ++i) {
                full[i] = static_cast<UInt>(random_value());
                low[i] = static_cast<UInt>(full[i] & width_mask<W>);
            }
            sp::detail::encode_tile<W, G>(low.data(), expected.data());
            sp::neon::encode_low_tile<W, G>(full.data(), encoded.data());
            if (encoded != expected) fail("low-bit projection", W, G, sizeof(UInt), trial);
            check_decoded<W, G>(encoded.data(), decoded.data(), low.data());
        }
    }
    ++families;
}

template<unsigned W, sp::geometry G>
void check_width();

template<unsigned W, sp::geometry G, class UInt>
[[gnu::noinline]] void check_narrow_input() {
    static_assert(sizeof(UInt) * 8 < W);
    constexpr unsigned T = sp::payload_layout<W, G>::tile_values;
    constexpr unsigned B = sp::payload_layout<W, G>::tile_bytes;
    std::array<UInt, T> values{};
    std::array<std::uint64_t, T> expanded{}, decoded{};
    std::array<std::uint8_t, B> encoded{}, expected{};
    const auto exercise = [&](const UInt* input, std::uint8_t* output) {
        for (unsigned i = 0; i != T; ++i) expanded[i] = input[i];
        sp::detail::encode_tile<W, G>(expanded.data(), expected.data());
        check_encoded<W, G>(input, output, expected.data());
        check_decoded<W, G>(output, decoded.data(), expanded.data());
    };
    exercise(values.data(), encoded.data());
    values.fill(~UInt(0));
    exercise(values.data(), encoded.data());
    values.fill(0);
    for (unsigned i = 0; i != T; ++i) {
        for (unsigned bit = 0; bit != sizeof(UInt) * 8; ++bit) {
            values[i] = static_cast<UInt>(std::uint64_t(1) << bit);
            exercise(values.data(), encoded.data());
        }
        values[i] = 0;
    }
    for (unsigned trial = 0; trial != 32; ++trial) {
        for (auto& value : values) value = static_cast<UInt>(random_value());
        exercise(values.data(), encoded.data());
    }
    for (unsigned offset = 0; offset != 64; ++offset) {
        std::array<std::uint8_t, B + 192> storage;
        storage.fill(0xa5);
        exercise(values.data(), storage.data() + 64 + offset);
        for (unsigned i = 0; i != storage.size(); ++i)
            if ((i < 64 + offset || i >= 64 + offset + B) && storage[i] != 0xa5)
                fail("narrow input write footprint", W, G, sizeof(UInt), i);
    }
    auto exact = std::make_unique<std::uint8_t[]>(B);
    auto exact_values = std::make_unique<UInt[]>(T);
    std::copy(values.begin(), values.end(), exact_values.get());
    exercise(exact_values.get(), exact.get());
    guarded_page tile_page, input_page;
    for (auto* p : {tile_page.begin(), tile_page.end() - B}) exercise(values.data(), p);
    for (auto* p : {input_page.begin(), input_page.end() - T * sizeof(UInt)}) {
        std::memcpy(p, values.data(), T * sizeof(UInt));
        exercise(reinterpret_cast<const UInt*>(p), encoded.data());
    }
    ++families;
}

template<unsigned W, sp::geometry G>
void check_width() {
    if constexpr (W <= 8) check_family<W, G, std::uint8_t>();
    else check_narrow_input<W, G, std::uint8_t>();
    if constexpr (W <= 16) check_family<W, G, std::uint16_t>();
    else check_narrow_input<W, G, std::uint16_t>();
    if constexpr (W <= 32) check_family<W, G, std::uint32_t>();
    else check_narrow_input<W, G, std::uint32_t>();
    check_family<W, G, std::uint64_t>();
}

template<unsigned W, class UInt>
[[gnu::noinline]] void check_pair() {
    std::array<UInt, 16> values{}, decoded{};
    std::array<std::uint8_t, 2 * W> encoded{}, expected{};
    const auto exercise = [&](std::uint8_t* out) {
        sp::detail::encode_tile<W, sp::geometry::local8>(values.data(), expected.data());
        sp::detail::encode_tile<W, sp::geometry::local8>(values.data() + 8, expected.data() + W);
        sp::neon::encode_local_pair<W>(values.data(), out);
        if (!std::equal(expected.begin(), expected.end(), out))
            fail("pair encode", W, sp::geometry::local8, sizeof(UInt), 0);
        sp::neon::decode_local_pair<W>(out, decoded.data());
        if (decoded != values) fail("pair decode", W, sp::geometry::local8, sizeof(UInt), 0);
        std::array<std::uint8_t, 16> bytes{};
        vst1q_u8(bytes.data(), (sp::neon::read_local_pair<W>(out)));
        for (unsigned i = 0; i != 16; ++i)
            if (bytes[i] != values[i]) fail("pair fragment", W, sp::geometry::local8, sizeof(UInt), i);
        ++cases;
    };
    for (unsigned bit = 0; bit != 16 * W; ++bit) {
        values.fill(0);
        values[bit / W] = static_cast<UInt>(1u << (bit % W));
        exercise(encoded.data());
    }
    for (unsigned trial = 0; trial != 64; ++trial) {
        for (auto& value : values) value = static_cast<UInt>(random_value() & width_mask<W>);
        exercise(encoded.data());
    }
    for (unsigned offset = 0; offset != 64; ++offset) {
        std::array<std::uint8_t, 128> storage;
        storage.fill(0xa5);
        exercise(storage.data() + 16 + offset);
        for (unsigned i = 0; i != storage.size(); ++i)
            if ((i < 16 + offset || i >= 16 + offset + 2 * W) && storage[i] != 0xa5)
                fail("pair write footprint", W, sp::geometry::local8, sizeof(UInt), i);
    }
    auto exact = std::make_unique<std::uint8_t[]>(2 * W);
    exercise(exact.get());
    guarded_page page;
    exercise(page.begin());
    exercise(page.end() - 2 * W);
    for (unsigned trial = 0; trial != 16; ++trial) {
        std::array<UInt, 16> low{};
        for (unsigned i = 0; i != 16; ++i) {
            values[i] = static_cast<UInt>(random_value());
            low[i] = static_cast<UInt>(values[i] & width_mask<W>);
        }
        sp::detail::encode_tile<W, sp::geometry::local8>(low.data(), expected.data());
        sp::detail::encode_tile<W, sp::geometry::local8>(low.data() + 8, expected.data() + W);
        sp::neon::encode_low_local_pair<W>(values.data(), encoded.data());
        if (encoded != expected) fail("pair low-bit projection", W, sp::geometry::local8, sizeof(UInt), trial);
        ++cases;
    }
    ++families;
}

template<unsigned W, class UInt>
[[gnu::noinline]] void check_dense_decode() {
    constexpr auto G = sp::geometry::local8;
    guarded_page input_page, output_page;
    for (std::size_t tiles = 0; tiles <= 33; ++tiles) {
        const std::size_t n = tiles * 8, bytes = tiles * W;
        auto wire = std::make_unique<std::uint8_t[]>(bytes);
        auto exact = std::make_unique<UInt[]>(n);
        std::vector<UInt> expected(n);
        const auto exercise = [&](const std::uint8_t* input, UInt* output) {
            sp::neon::decode_tiles<W, G>(input, output, tiles);
            if (!std::equal(expected.begin(), expected.end(), output))
                fail("dense decode", W, G, sizeof(UInt), tiles);
            ++cases;
        };
        for (unsigned trial = 0; trial != 8; ++trial) {
            std::fill_n(wire.get(), bytes, 0);
            for (std::size_t i = 0; i != n; ++i) {
                expected[i] = UInt((trial == 0 ? 0 : trial == 1 ? ~std::uint64_t(0) : random_value()) & width_mask<W>);
                for (unsigned bit = 0; bit != W; ++bit)
                    wire[(i / 8) * W + bit] |= std::uint8_t(((expected[i] >> bit) & 1) << (i % 8));
            }
            exercise(wire.get(), exact.get());
        }
        for (unsigned offset = 0; offset != 64 / sizeof(UInt); ++offset) {
            std::vector<UInt> storage(n + 128, UInt(0xa5));
            exercise(wire.get(), storage.data() + 32 + offset);
            for (std::size_t i = 0; i != storage.size(); ++i)
                if ((i < 32 + offset || i >= 32 + offset + n) && storage[i] != UInt(0xa5))
                    fail("dense decode footprint", W, G, sizeof(UInt), i);
        }
        for (unsigned offset = 0; offset != 64; ++offset) {
            std::vector<std::uint8_t> storage(bytes + 64);
            std::copy_n(wire.get(), bytes, storage.data() + offset);
            exercise(storage.data() + offset, exact.get());
        }
        for (auto* input : {input_page.begin(), input_page.end() - bytes}) {
            std::memcpy(input, wire.get(), bytes);
            for (auto* output : {output_page.begin(), output_page.end() - n * sizeof(UInt)})
                exercise(input, reinterpret_cast<UInt*>(output));
        }
    }
    sp::neon::decode_tiles<W, G>(nullptr, static_cast<UInt*>(nullptr), 0);
    ++families;
}

template<unsigned W, class UInt, bool WidthValid = false>
[[gnu::noinline]] void check_dense_encode() {
    static_assert(W >= 1 && W <= 8 && (!WidthValid || W == 1));
    constexpr auto G = sp::geometry::local8;
    guarded_page input_page, output_page;
    for (std::size_t tiles = 0; tiles <= 33; ++tiles) {
        const std::size_t n = tiles * 8, bytes = tiles * W;
        auto input = std::make_unique<UInt[]>(n);
        auto exact = std::make_unique<std::uint8_t[]>(bytes);
        std::vector<std::uint8_t> expected(bytes);
        const auto exercise = [&](const UInt* values, std::uint8_t* output) {
            if constexpr (WidthValid) sp::neon::encode_local1_tiles(values, output, tiles);
            else sp::neon::encode_low_tiles<W, G>(values, output, tiles);
            if (!std::equal(expected.begin(), expected.end(), output))
                fail("dense encode", W, G, sizeof(UInt), tiles);
            ++cases;
        };
        const unsigned source_bits = sizeof(UInt) * 8;
        const bool basis_family = (W >= 3 && sizeof(UInt) == 1) || (W == 8 && sizeof(UInt) == 8);
        const unsigned basis = (basis_family && (tiles == 8 || tiles == 9)) ? n * source_bits : 0;
        for (unsigned trial = 0; trial != 8 + basis; ++trial) {
            std::fill(expected.begin(), expected.end(), 0);
            for (std::size_t i = 0; i != n; ++i) {
                if (trial >= 8)
                    input[i] = i == (trial - 8) / source_bits
                        ? UInt(std::uint64_t(1) << ((trial - 8) % source_bits)) : UInt(0);
                else input[i] = trial == 0 ? UInt(0) : trial == 1 ? ~UInt(0) : UInt(random_value());
                if constexpr (WidthValid) input[i] &= 1;
                // Independent plane-bit oracle, including deliberately ignored
                // high bits and all source-bit bases across a region/remainder.
                if constexpr (W == 8) expected[i] = std::uint8_t(input[i]);
                else for (unsigned bit = 0; bit != W; ++bit)
                    expected[(i / 8) * W + bit] |= std::uint8_t(((input[i] >> bit) & 1) << (i % 8));
            }
            exercise(input.get(), exact.get());
        }
        for (unsigned offset = 0; offset != 64; ++offset) {
            std::vector<std::uint8_t> storage(bytes + 128, 0xa5);
            auto* out = storage.data() + 32 + offset;
            exercise(input.get(), out);
            for (std::size_t i = 0; i != storage.size(); ++i)
                if ((i < 32 + offset || i >= 32 + offset + bytes) && storage[i] != 0xa5)
                    fail("dense encode footprint", W, G, sizeof(UInt), i);
        }
        for (unsigned offset = 0; offset != 64 / sizeof(UInt); ++offset) {
            std::vector<UInt> storage(n + 64);
            std::copy_n(input.get(), n, storage.data() + offset);
            exercise(storage.data() + offset, exact.get());
        }
        for (auto* in : {input_page.begin(), input_page.end() - n * sizeof(UInt)}) {
            std::memcpy(in, input.get(), n * sizeof(UInt));
            for (auto* out : {output_page.begin(), output_page.end() - bytes})
                exercise(reinterpret_cast<const UInt*>(in), out);
        }
    }
    if constexpr (WidthValid) sp::neon::encode_local1_tiles(static_cast<const UInt*>(nullptr), nullptr, 0);
    else sp::neon::encode_low_tiles<W, G>(static_cast<const UInt*>(nullptr), nullptr, 0);
    sp::neon::encode_low_tiles<0, G>(static_cast<const UInt*>(nullptr), nullptr, 17);
    ++families;
}

} // namespace

int main() {
    sp::detail::static_for<65>([](auto w) {
        check_width<w, sp::geometry::local8>();
        if constexpr (sp::supports_stripes(w)) check_width<w, sp::geometry::striped>();
    });
    sp::detail::static_for<7>([](auto w) {
        check_pair<w + 1, std::uint8_t>();
        check_pair<w + 1, std::uint16_t>();
        check_pair<w + 1, std::uint32_t>();
        check_pair<w + 1, std::uint64_t>();
    });
    check_dense_encode<1, std::uint8_t, true>();
    check_dense_encode<1, std::uint16_t, true>();
    check_dense_encode<1, std::uint32_t, true>();
    check_dense_encode<1, std::uint64_t, true>();
    check_dense_encode<8, std::uint8_t>();
    check_dense_encode<8, std::uint16_t>();
    check_dense_encode<8, std::uint32_t>();
    check_dense_encode<8, std::uint64_t>();
    sp::detail::static_for<7>([](auto w) {
        check_dense_encode<w + 1, std::uint8_t>();
        check_dense_encode<w + 1, std::uint16_t>();
        check_dense_encode<w + 1, std::uint32_t>();
        check_dense_encode<w + 1, std::uint64_t>();
        check_dense_decode<w + 1, std::uint8_t>();
        check_dense_decode<w + 1, std::uint16_t>();
        check_dense_decode<w + 1, std::uint32_t>();
        check_dense_decode<w + 1, std::uint64_t>();
    });
    std::printf("NEON payload: %u width/geometry/lane families, %llu cases passed\n",
                families, static_cast<unsigned long long>(cases));
}

#else

#include <cstdio>
int main() { std::puts("NEON payload: skipped on non-AArch64 target"); }

#endif
