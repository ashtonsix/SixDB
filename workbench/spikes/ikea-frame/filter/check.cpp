#include "evidence.h"
#include <array>
#include <csignal>
#include <random>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
namespace ff = frame_filter;
namespace fe = frame_encoding;
using C = ikea::comparison;
using ff::u64;
void need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
bool compare(u64 x, u64 y, C op) {
    switch (op) {
    case C::equal: return x == y;
    case C::less: return x < y;
    case C::less_equal: return x <= y;
    case C::greater: return x > y;
    case C::greater_equal: return x >= y;
    }
    std::unreachable();
}
constexpr std::array ops{C::equal, C::less, C::less_equal, C::greater, C::greater_equal};
std::size_t all_widths() {
    std::mt19937_64 rng(713);
    std::size_t checks = 0;
    for (unsigned k = 1; k <= 64; ++k) {
        const u64 max = UINT64_MAX >> (64 - k);
        std::vector<u64> values(263);
        for (auto &v : values) v = rng() & max;
        values[0] = 0; values[1] = max;
        std::array<u64, 8> queries{0, 1, max / 2, max, max == UINT64_MAX ? max : max + 1,
                                   UINT64_MAX, values[17], values[262]};
        for (unsigned h : {0u, 8u, 16u}) {
            if (k < h) continue;
            std::vector<std::uint8_t> p(fe::plane_bytes(k - h, values.size()));
            std::vector<std::uint8_t> a(h >= 8 ? fe::plane_bytes(8, values.size()) : 0);
            std::vector<std::uint8_t> b(h == 16 ? a.size() : 0);
            const auto &kernel = ff::kernels(k, h);
            kernel.pack(values, p.data(), a.data(), b.data());
            const ff::planes source{p.data(), a.data(), b.data(), values.size()};
            for (unsigned n : {1u, 17u, 256u}) {
                std::array<std::uint8_t, 256> ends;
                std::array<unsigned, 256> lengths;
                unsigned limit = 0;
                for (unsigned r = 0; r < n; ++r) {
                    lengths[r] = n == 1 ? 256 : n == 256 ? 1 : r == 16 ? 120 : r + 1;
                    limit += lengths[r]; ends[r] = limit - 1;
                }
                need(limit == 256, "authored run geometry differs");
                const ff::planes complete{p.data(), a.data(), b.data(), n};
                for (auto query : queries) for (bool guard : {false, true}) {
                    u64 plain = 0, weighted = 0;
                    for (unsigned r = 0; r < n; ++r) {
                        plain += values[r] >= query;
                        if (values[r] >= query) weighted += lengths[r];
                    }
                    need(kernel.count_ge(complete, query, guard, nullptr) == plain,
                         "fused plane COUNT differs from original integers");
                    need(kernel.count_ge(complete, query, guard, ends.data()) == weighted,
                         "fused weighted COUNT differs from original run lengths");
                    checks += 2;
                }
            }
            for (auto first : {0u, 1u, 7u, 8u, 16u, 197u, 255u})
                for (auto n : {0u, 1u, 7u, 8u, 15u, 16u, 63u, 64u}) {
                    if (first + n > values.size()) continue;
                    for (auto query : queries) for (auto op : ops) {
                        u64 expected = 0;
                        for (unsigned i = 0; i < n; ++i)
                            expected |= u64(compare(values[first + i], query, op)) << i;
                        for (bool guard : {false, true}) {
                            need(kernel.filter(source, first, n, query, op, guard) == expected,
                                 "split-plane filter differs from original integers");
                            ++checks;
                        }
                    }
                }
        }
    }
    return checks;
}
struct pages {
    std::size_t page = std::size_t(::sysconf(_SC_PAGESIZE));
    std::uint8_t *data = static_cast<std::uint8_t *>(::mmap(
        nullptr, 3 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    pages() { need(data != MAP_FAILED && page >= 2048, "mapping failed"); }
    ~pages() { ::munmap(data, 3 * page); }
    void protect(unsigned i, int p) { need(::mprotect(data + i * page, page, p) == 0, "protection failed"); }
};
template <class F> void must_fault(F read) {
    auto child = ::fork();
    need(child >= 0, "fork failed");
    if (!child) {
        const rlimit no_core{0, 0};
        if (::setrlimit(RLIMIT_CORE, &no_core)) ::_exit(120);
        fe::consume(read()); ::_exit(0);
    }
    int status = 0;
    need(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
             (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS),
         "injected live suffix read did not fault");
}
void protected_planes() {
    for (unsigned h : {8u, 16u}) {
        pages mem;
        const auto &k = ff::kernels(32, h);
        auto *payload = mem.data, *head0 = mem.data + mem.page, *head1 = mem.data + 2 * mem.page;
        std::array<u64, 256> values;
        for (unsigned i = 0; i < values.size(); ++i) values[i] = 0x12000000u + i;
        k.pack(values, payload, head0, head1);
        ff::planes source{payload, head0, head1, values.size()};
        mem.protect(0, PROT_NONE);
        mem.protect(2, PROT_NONE);
        for (auto op : ops) {
            const u64 expected = compare(values[0], 0x20000000u, op) ? UINT64_MAX : 0;
            need(k.filter(source, 0, 64, 0x20000000u, op, true) == expected,
                 "first head did not resolve without suffix");
        }
        need(k.count_ge(source, 0x20000000u, true, nullptr) == 0,
             "fused head rejection read suffix");
        if (h == 16) {
            mem.protect(2, PROT_READ);
            for (auto op : ops) {
                const u64 expected = compare(values[0], 0x12800000u, op) ? UINT64_MAX : 0;
                need(k.filter(source, 0, 64, 0x12800000u, op, true) == expected,
                     "second head did not resolve without payload");
            }
        }
        for (unsigned i = 0; i < 3; ++i) mem.protect(i, PROT_NONE);
        need(k.filter(source, 0, 0, 0, C::equal, true) == 0, "empty request read a plane");
        source.count = 0;
        need(k.count_ge(source, 0, true, mem.data) == 0, "empty COUNT read a plane");
        source.count = values.size();
        mem.protect(1, PROT_READ);
        if (h == 16) mem.protect(2, PROT_READ);
        must_fault([&] { return k.filter(source, 0, 64, values[0], C::equal, true); });
    }
    // Exactly seventeen readable endpoint bytes; the second native window is
    // forbidden. The legitimate tail is scalar and the injected larger grant
    // must expose a read beyond that tail.
    pages end_storage;
    end_storage.protect(1, PROT_NONE);
    auto *ends = end_storage.data + end_storage.page - 17;
    unsigned limit = 0;
    std::array<u64, 32> values{};
    for (unsigned r = 0; r < 17; ++r) {
        limit += r == 16 ? 120 : r + 1;
        ends[r] = limit - 1;
        values[r] = r;
    }
    std::array<std::uint8_t, 32> payload{};
    const auto &k = ff::kernels(8, 0);
    k.pack(values, payload.data(), nullptr, nullptr);
    ff::planes source{payload.data(), nullptr, nullptr, 17};
    need(k.count_ge(source, 8, false, ends) == 220, "exact weighted tail differs");
    source.count = 32;
    must_fault([&] { return k.count_ge(source, 0, false, ends); });
}
void protected_none() {
    std::array<u64, 256> values;
    for (unsigned r = 0; r < 256; ++r) values[r] = (r * 73) % 251;
    for (auto dictionary : {fe::dictionary_order::none, fe::dictionary_order::sorted,
                            fe::dictionary_order::frequency}) {
        pages storage;
        auto *column = std::construct_at(reinterpret_cast<fe::column *>(storage.data),
            fe::encode(values, {.dictionary = dictionary, .dictionary_every_part = true}));
        ff::image image(*column, 8);
        ff::logical_heads part8(*column, 8), part16(*column, 16);
        ff::window_bounds bounds(*column);
        ff::block_bounds coarse_bounds(*column);
        fe::block_mask none(1), broken(1);
        broken.set(0);
        storage.protect(0, PROT_NONE);
        need(image.count_ge(none, 100, true) == 0, "none predicate read column metadata");
        need(part8.count_ge(none, 100) == 0 && part16.count_ge(none, 100) == 0 &&
                 bounds.count_ge(none, 100) == 0 && coarse_bounds.count_ge(none, 100) == 0,
             "none evidence query read column metadata");
        must_fault([&] { return image.count_ge(broken, 100, true); });
        storage.protect(0, PROT_READ | PROT_WRITE);
        std::destroy_at(column);
    }
}
void protected_evidence() {
    pages storage;
    std::array<u64, 256> values;
    // Every native window lies wholly below or above the middle threshold.
    // Both heads and bounds resolve it while the entire column is unreadable.
    for (unsigned r = 0; r < 256; ++r)
        values[r] = (r / 16 % 2 ? 0x22670000u : 0x12340000u) + r;
    auto *column = std::construct_at(reinterpret_cast<fe::column *>(storage.data), fe::encode(values));
    {
        ff::logical_heads h8(*column, 8), h16(*column, 16);
        ff::window_bounds bounds(*column);
        ff::block_bounds coarse_bounds(*column);
        fe::block_mask all(1); all.set(0);
        storage.protect(0, PROT_NONE);
        need(h8.count_ge(all, 0x1a000000u) == 128 &&
                 h16.count_ge(all, 0x1a000000u) == 128 &&
                 bounds.count_ge(all, 0x1a000000u) == 128,
             "resolved evidence query read the encoded column");
        need(coarse_bounds.count_ge(all, 0) == 256 && coarse_bounds.count_ge(all, UINT64_MAX) == 0,
             "resolved block bounds read the encoded column");
        must_fault([&] { return coarse_bounds.count_ge(all, 0x1a000000u); });
        // A tie at a nonzero suffix now genuinely requires the body.
        must_fault([&] { return h8.count_ge(all, values[1]); });
        must_fault([&] { return h16.count_ge(all, values[1]); });
        must_fault([&] { return bounds.count_ge(all, values[1]); });
        storage.protect(0, PROT_READ | PROT_WRITE);
    }
    std::destroy_at(column);
}
std::vector<u64> load(const char *path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    need(bool(in), "input missing");
    auto bytes = static_cast<std::size_t>(in.tellg());
    need(bytes && bytes % (256 * sizeof(u64)) == 0, "input must contain complete blocks");
    std::vector<u64> values(bytes / sizeof(u64));
    in.seekg(0);
    need(bool(in.read(reinterpret_cast<char *>(values.data()), bytes)), "short read");
    return values;
}
std::size_t whole_image(const std::vector<u64> &values, bool delta_exceptions = false,
                       fe::dictionary_order dictionary = fe::dictionary_order::none) {
    std::size_t checks = 0;
    auto column = fe::encode(values, {.delta_checkpoints = fe::checkpoints::native,
        .delta_exceptions = delta_exceptions, .dictionary = dictionary});
    ff::image headless(column, 0), head8(column, 8), head16(column, 16);
    ff::image early8(column, 8, ff::split_stage::working64);
    ff::image early16(column, 16, ff::split_stage::working64);
    ff::logical_heads part8(column, 8), part16(column, 16);
    ff::window_bounds bounds(column);
    ff::block_bounds coarse_bounds(column);
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    for (auto rhs : {u64{0}, *lo, *lo + (*hi - *lo) / 2, *hi, UINT64_MAX})
        for (unsigned pattern = 0; pattern < 4; ++pattern) {
            fe::block_mask mask(column.blocks.size());
            for (std::size_t b = 0; b < mask.blocks; ++b)
                if (!pattern || (pattern == 2 && b % 2) || (pattern == 3 && b / 16 % 2))
                    mask.set(b);
            u64 expected = 0;
            for (std::size_t r = 0; r < values.size(); ++r)
                if ((mask.words[r / 256 / 64] >> (r / 256 % 64)) & 1)
                    expected += values[r] >= rhs;
            need(fe::encoded_count_ge(column, mask, rhs) == expected, "decode control differs");
            need(part8.count_ge(mask, rhs) == expected && part16.count_ge(mask, rhs) == expected &&
                     bounds.count_ge(mask, rhs) == expected && coarse_bounds.count_ge(mask, rhs) == expected,
                 "logical heads/bounds differ from original values");
            checks += 4;
            for (const auto *image : {&headless, &head8, &head16, &early8, &early16})
                for (bool guard : {false, true}) {
                    need(image->count_ge(mask, rhs, guard) == expected,
                         "whole-image split predicate differs from original input");
                    ++checks;
                }
        }
    return checks;
}
} // namespace
int main(int argc, char **argv) {
    const bool dictionaries = std::any_of(argv + 1, argv + argc,
        [](const char *arg) { return std::string_view(arg) == "--dictionaries"; });
    auto checks = all_widths();
    protected_planes();
    protected_none();
    protected_evidence();
    std::vector<u64> authored(6 * 256);
    std::mt19937_64 rng(713);
    u64 up = 1000, down = UINT64_MAX - 1000;
    for (unsigned i = 0; i < 256; ++i) {
        authored[i] = 999;
        authored[256 + i] = (i / 16 % 3) * 100000;
        authored[512 + i] = rng() % 256;
        authored[768 + i] = up += 5 + rng() % 3;
        authored[1024 + i] = down -= 5 + rng() % 3;
        authored[1280 + i] = rng() % 256;
    }
    authored[1280 + 17] = u64{1} << 60;
    authored[1280 + 199] = u64{1} << 61;
    checks += whole_image(authored);
    checks += whole_image(authored, true);
    for (auto order : {fe::dictionary_order::sorted, fe::dictionary_order::frequency})
        checks += whole_image(authored, true, order);
    // Boundaries through 64-bit values and multiple/partial parts. The expected
    // predicates use original integers rather than either summary's arithmetic.
    for (unsigned width = 1; width <= 64; ++width) {
        std::vector<u64> values((width == 64 ? 258 : 2) * 256);
        const u64 max = UINT64_MAX >> (64 - width);
        for (auto &value : values) value = rng() & max;
        values[0] = 0; values[1] = max;
        checks += whole_image(values);
    }
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--dictionaries") continue;
        auto values = load(argv[i]);
        checks += whole_image(values);
        checks += whole_image(values, true);
        if (dictionaries)
            for (auto order : {fe::dictionary_order::sorted, fe::dictionary_order::frequency})
                checks += whole_image(values, true, order);
    }
    std::printf("%zu exact split-plane/whole-image predicate checks; 64 widths, five predicates, "
                "both guards, tails/coarse masks; protected heads/suffix and injected faults pass.\n", checks);
}
