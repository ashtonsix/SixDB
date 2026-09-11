#include <ikea/seriespack/detail/body_neon.h>

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

namespace {

namespace body = ikea::seriespack::detail::neon;

std::uint64_t cases = 0;
std::uint64_t random_state = 0x295dfb4d18ffae03;

std::uint64_t random_value() {
    random_state ^= random_state >> 12;
    random_state ^= random_state << 25;
    random_state ^= random_state >> 27;
    return random_state * 0x2545f4914f6cdd1d;
}

[[noreturn]] void fail(const char* operation, unsigned q, unsigned lane_bytes,
                       unsigned position) {
    std::fprintf(stderr, "%s: Q=%u LaneBytes=%u position=%u case=%llu\n",
                 operation, q, lane_bytes, position,
                 static_cast<unsigned long long>(cases));
    std::abort();
}

// The oracle interprets little-endian integer words. It does not use a native
// shuffle map, the implementation's masks, or a production scalar codec.
std::uint64_t word(const std::uint8_t* p, unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned b = 0; b != bytes; ++b) value |= std::uint64_t(p[b]) << (8 * b);
    return value;
}

template<unsigned Q>
constexpr std::uint64_t body_mask = [] {
    if constexpr (Q == 8) return ~std::uint64_t(0);
    else return (std::uint64_t(1) << (8 * Q)) - 1;
}();

template<unsigned Q, unsigned L>
void check_decode(const std::uint8_t* encoded) {
    std::array<std::uint8_t, 16> decoded{};
    vst1q_u8(decoded.data(), (body::decode_body<Q, L>(encoded)));
    for (unsigned i = 0; i != 16 / L; ++i)
        if (word(decoded.data() + i * L, L) != word(encoded + i * Q, Q))
            fail("decode", Q, L, i);
    ++cases;
}

template<unsigned Q, unsigned L>
void check_encode(std::uint8_t* encoded, const std::uint8_t* values) {
    body::encode_body<Q, L>(encoded, vld1q_u8(values));
    for (unsigned i = 0; i != 16 / L; ++i)
        if (word(encoded + i * Q, Q) != (word(values + i * L, L) & body_mask<Q>))
            fail("encode", Q, L, i);
    ++cases;
}

template<unsigned Q, unsigned L, std::size_t... P>
auto decode_packet(const std::uint8_t* encoded, std::index_sequence<P...>) {
    return std::array<uint8x16_t, L / 2>{body::decode_packet_body<Q, L, P>(encoded)...};
}

template<unsigned Q, unsigned L>
void check_packet_decode(const std::uint8_t* encoded) {
    auto fragments = decode_packet<Q, L>(encoded, std::make_index_sequence<L / 2>{});
    for (unsigned part = 0; part != L / 2; ++part) {
        std::array<std::uint8_t, 16> decoded{};
        vst1q_u8(decoded.data(), fragments[part]);
        for (unsigned i = 0; i != 16 / L; ++i) {
            const unsigned original = part * (16 / L) + i;
            if (word(decoded.data() + i * L, L) != word(encoded + original * Q, Q))
                fail("packet decode", Q, L, original);
        }
    }
    ++cases;
}

template<unsigned Q, unsigned L>
void check_packet_encode(std::uint8_t* encoded, const std::uint8_t* values) {
    std::array<uint8x16_t, L / 2> fragments{};
    for (unsigned part = 0; part != L / 2; ++part)
        fragments[part] = vld1q_u8(values + 16 * part);
    body::encode_packet_body<Q, L>(encoded, fragments);
    for (unsigned i = 0; i != 8; ++i)
        if (word(encoded + i * Q, Q) != (word(values + i * L, L) & body_mask<Q>))
            fail("packet encode", Q, L, i);
    ++cases;
}

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

template<unsigned Q, unsigned L, unsigned Count>
void check_prefix() {
    constexpr unsigned extent = Q * Count;
    std::array<std::uint8_t, extent> encoded{};
    for (auto& byte : encoded) byte = static_cast<std::uint8_t>(random_value());
    const auto check = [&](const std::uint8_t* p) {
        std::array<std::uint8_t, 16> decoded{};
        vst1q_u8(decoded.data(), (body::decode_body_prefix<Q, L, Count>(p)));
        for (unsigned i = 0; i != 16 / L; ++i) {
            const auto expected = i < Count ? word(encoded.data() + i * Q, Q) : 0;
            if (word(decoded.data() + i * L, L) != expected) fail("prefix", Q, L, i);
        }
        ++cases;
    };
    if constexpr (Count == 0) check(nullptr);
    else {
        auto exact = std::make_unique<std::uint8_t[]>(extent);
        std::copy(encoded.begin(), encoded.end(), exact.get());
        check(exact.get());
        guarded_page page;
        for (auto* p : {page.begin(), page.end() - extent}) {
            std::copy(encoded.begin(), encoded.end(), p);
            check(p);
        }
    }
}

template<unsigned Q, unsigned L>
void check_family() {
    constexpr unsigned extent = (16 / L) * Q;
    std::array<std::uint8_t, 16> values{};
    std::array<std::uint8_t, 16> encoded{};

    // Each possible encoded bit and each native carrier bit gets its own case.
    // Carrier bits above Q must be ignored by encode, never leak into neighbors.
    for (unsigned bit = 0; bit != 8 * extent; ++bit) {
        encoded.fill(0);
        encoded[bit / 8] = std::uint8_t(1u << (bit % 8));
        check_decode<Q, L>(encoded.data());
    }
    for (unsigned bit = 0; bit != 128; ++bit) {
        values.fill(0);
        values[bit / 8] = std::uint8_t(1u << (bit % 8));
        check_encode<Q, L>(encoded.data(), values.data());
    }

    for (unsigned trial = 0; trial != 256; ++trial) {
        for (auto& byte : values) byte = static_cast<std::uint8_t>(random_value());
        for (auto& byte : encoded) byte = static_cast<std::uint8_t>(random_value());
        check_decode<Q, L>(encoded.data());
        check_encode<Q, L>(encoded.data(), values.data());
    }

    // Every offset modulo a cache line; stores leave both neighboring ranges intact.
    for (unsigned offset = 0; offset != 64; ++offset) {
        std::array<std::uint8_t, 256> storage;
        storage.fill(0xa5);
        auto* p = storage.data() + 64 + offset;
        check_encode<Q, L>(p, values.data());
        check_decode<Q, L>(p);
        for (unsigned i = 0; i != storage.size(); ++i)
            if ((i < 64 + offset || i >= 64 + offset + extent) && storage[i] != 0xa5)
                fail("fragment write footprint", Q, L, i);
    }

    // Exact allocations expose every out-of-extent access to ASan. Protected
    // preceding/following pages exercise the same endpoints without a sanitizer.
    auto exact = std::make_unique<std::uint8_t[]>(extent);
    check_encode<Q, L>(exact.get(), values.data());
    check_decode<Q, L>(exact.get());
    guarded_page page;
    for (auto* p : {page.begin(), page.end() - extent}) {
        check_encode<Q, L>(p, values.data());
        check_decode<Q, L>(p);
    }
    [&]<std::size_t... Count>(std::index_sequence<Count...>) {
        (check_prefix<Q, L, Count>(), ...);
    }(std::make_index_sequence<16 / L + 1>{});

    if constexpr (L >= 2) {
        constexpr unsigned packet_extent = 8 * Q;
        std::array<std::uint8_t, 8 * L> packet_values{};
        std::array<std::uint8_t, packet_extent> packet{};
        for (unsigned bit = 0; bit != 8 * packet_extent; ++bit) {
            packet.fill(0);
            packet[bit / 8] = std::uint8_t(1u << (bit % 8));
            check_packet_decode<Q, L>(packet.data());
        }
        for (unsigned bit = 0; bit != 8 * packet_values.size(); ++bit) {
            packet_values.fill(0);
            packet_values[bit / 8] = std::uint8_t(1u << (bit % 8));
            check_packet_encode<Q, L>(packet.data(), packet_values.data());
        }
        for (unsigned trial = 0; trial != 256; ++trial) {
            for (auto& byte : packet_values) byte = static_cast<std::uint8_t>(random_value());
            for (auto& byte : packet) byte = static_cast<std::uint8_t>(random_value());
            check_packet_decode<Q, L>(packet.data());
            check_packet_encode<Q, L>(packet.data(), packet_values.data());
        }
        for (unsigned offset = 0; offset != 64; ++offset) {
            std::array<std::uint8_t, 256> storage;
            storage.fill(0xa5);
            auto* p = storage.data() + 64 + offset;
            check_packet_encode<Q, L>(p, packet_values.data());
            check_packet_decode<Q, L>(p);
            for (unsigned i = 0; i != storage.size(); ++i)
                if ((i < 64 + offset || i >= 64 + offset + packet_extent) && storage[i] != 0xa5)
                    fail("packet write footprint", Q, L, i);
        }
        auto packet_exact = std::make_unique<std::uint8_t[]>(packet_extent);
        check_packet_encode<Q, L>(packet_exact.get(), packet_values.data());
        check_packet_decode<Q, L>(packet_exact.get());
        for (auto* p : {page.begin(), page.end() - packet_extent}) {
            check_packet_encode<Q, L>(p, packet_values.data());
            check_packet_decode<Q, L>(p);
        }
    }
}

template<unsigned L, std::size_t... I>
void check_lane(std::index_sequence<I...>) { (check_family<I + 1, L>(), ...); }

} // namespace

int main() {
    check_lane<1>(std::make_index_sequence<1>{});
    check_lane<2>(std::make_index_sequence<2>{});
    check_lane<4>(std::make_index_sequence<4>{});
    check_lane<8>(std::make_index_sequence<8>{});
    std::printf("NEON body: 15 width/lane families, %llu cases passed\n",
                static_cast<unsigned long long>(cases));
}

#else

#include <cstdio>
int main() { std::puts("NEON body: skipped on non-AArch64 target"); }

#endif
