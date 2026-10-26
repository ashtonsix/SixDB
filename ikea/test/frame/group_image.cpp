#include <ikea/frame/detail/group_image.h>

#include <ikea/seriespack/presets.h>
#include <ikea/seriespack/reader.h>
#include <ikea/seriespack/writer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <source_location>
#include <span>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fo = ikea::frame::detail;
namespace sp = ikea::seriespack;
namespace {
using u64 = std::uint64_t;
std::size_t valid_cases{}, truncated_cases{}, malformed_cases{};
void check(bool condition, const char *message,
    std::source_location where = std::source_location::current()) {
    if (condition) return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), message);
    std::abort();
}
template <class U> void put(std::vector<std::byte> &bytes, U value) {
    for (unsigned i = 0; i < sizeof(U); ++i) bytes.push_back(std::byte((value >> (i * 8)) & 255));
}
template <class U> void overwrite(std::vector<std::byte> &bytes, std::size_t at, U value) {
    check(at <= bytes.size() && sizeof(U) <= bytes.size() - at, "fixture overwrite outside bytes");
    for (unsigned i = 0; i < sizeof(U); ++i) bytes[at + i] = std::byte((value >> (i * 8)) & 255);
}
u64 mask(unsigned width) { return width == 64 ? ~u64{0} : width ? (u64{1} << width) - 1 : 0; }
sp::preset preset(fo::image_preset value) {
    switch (value) {
    case fo::image_preset::compact: return sp::preset::compact;
    case fo::image_preset::bulk_x86: return sp::preset::bulk_x86;
    case fo::image_preset::bulk_arm: return sp::preset::bulk_arm;
    }
    std::abort();
}

// A reusable readable area with inaccessible pages on either side. Every
// admitted image/prefix ends exactly at the upper inaccessible page.
class guarded {
    std::byte *mapping_, *begin_, *end_;
    std::size_t size_, capacity_;
  public:
    explicit guarded(std::size_t capacity) {
        const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
        capacity_ = ((capacity + page - 1) / page) * page;
        size_ = capacity_ + 2 * page;
        void *mapping = ::mmap(nullptr, size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        check(mapping != MAP_FAILED, "mmap guard failed");
        mapping_ = static_cast<std::byte *>(mapping);
        begin_ = mapping_ + page;
        check(::mprotect(begin_, capacity_, PROT_READ | PROT_WRITE) == 0, "mprotect body failed");
        end_ = begin_ + capacity_;
    }
    ~guarded() { check(::munmap(mapping_, size_) == 0, "munmap failed"); }
    guarded(const guarded &) = delete;
    std::span<const std::byte> place(std::span<const std::byte> bytes) {
        check(bytes.size() <= capacity_, "fixture exceeds guard area");
        check(::mprotect(begin_, capacity_, PROT_READ | PROT_WRITE) == 0, "mprotect fixture failed");
        if (!bytes.empty()) std::memcpy(end_ - bytes.size(), bytes.data(), bytes.size());
        check(::mprotect(begin_, capacity_, PROT_READ) == 0, "mprotect immutable image failed");
        return {end_ - bytes.size(), bytes.size()};
    }
    const std::byte *inaccessible() const { return end_; }
};

struct block {
    unsigned width;
    u64 lo;
    std::vector<u64> fields;
    std::vector<unsigned> exceptions;
};
struct fixture {
    std::vector<std::byte> bytes;
    std::array<std::size_t, 65> array_header{};
    std::size_t capacity_offset{}, positions{}, values{}, leaf{}, array_count{};
    unsigned blocks{};
};
block make_block(unsigned width, unsigned rows, bool exceptions) {
    block b{width, ~u64{0} - mask(width), std::vector<u64>(rows), {}};
    for (unsigned r = 0; r < rows; ++r)
        b.fields[r] = r % 3 == 0 ? 0 : r % 3 == 1 ? mask(width) : (u64{r} * 0x9e3779b97f4a7c15) & mask(width);
    if (exceptions) {
        b.exceptions = {0, rows / 2, rows - 1};
        std::sort(b.exceptions.begin(), b.exceptions.end());
        b.exceptions.erase(std::unique(b.exceptions.begin(), b.exceptions.end()), b.exceptions.end());
        for (unsigned r : b.exceptions) b.fields[r] = 0;
    }
    return b;
}

fixture encode(std::span<const block> blocks, fo::image_preset p, bool bounded) {
    fixture out;
    out.blocks = static_cast<unsigned>(blocks.size());
    std::array<std::vector<u64>, 65> streams;
    std::array<unsigned, 65> slots{}, counts{};
    std::array<bool, 65> present{};
    unsigned used = 0;
    if (bounded) {
        for (const auto &b : blocks) {
            put<u64>(out.bytes, b.lo);
            put<std::uint8_t>(out.bytes, b.width);
            put<std::uint16_t>(out.bytes, used);
            put<std::uint16_t>(out.bytes, b.exceptions.size());
            used += b.exceptions.size();
        }
        out.capacity_offset = out.bytes.size();
        put<std::uint16_t>(out.bytes, used + 2);
        put<std::uint16_t>(out.bytes, used);
        out.positions = out.bytes.size();
        for (const auto &b : blocks)
            for (unsigned r : b.exceptions) put<std::uint8_t>(out.bytes, r);
        put<std::uint8_t>(out.bytes, 255), put<std::uint8_t>(out.bytes, 255);  // unused slots need no row meaning
        out.values = out.bytes.size();
        for (const auto &b : blocks)
            for (unsigned r : b.exceptions) put<u64>(out.bytes, r % 2 ? ~u64{0} : 0);
        put<u64>(out.bytes, ~u64{0}), put<u64>(out.bytes, ~u64{0});
    }
    out.leaf = out.bytes.size();
    for (const auto &b : blocks) {
        put<std::uint8_t>(out.bytes, b.width);
        put<std::uint8_t>(out.bytes, b.width ? slots[b.width]++ : 0);
        put<std::uint16_t>(out.bytes, b.fields.size());
        present[b.width] = true;
        if (b.width) {
            streams[b.width].insert(streams[b.width].end(), b.fields.begin(), b.fields.end());
            counts[b.width] += b.fields.size();
        }
    }
    out.array_count = out.bytes.size();
    put<std::uint8_t>(out.bytes, std::count(present.begin(), present.end(), true));
    for (unsigned k = 0; k <= 64; ++k) {
        if (!present[k]) continue;
        std::vector<std::byte> body;
        if (k) {
            const auto geometry = sp::resolve(preset(p), k);
            const std::size_t tile_rows = geometry == sp::geometry::local ? 8 : 32 * (8 / std::gcd(k % 8, 8u));
            body.resize(((streams[k].size() + tile_rows - 1) / tile_rows) * (tile_rows * k / 8));
            const sp::dense_output target{k, geometry, body, streams[k].size()};
            auto capacity = sp::effect_capacity_initialize(target);
            check(bool(capacity), "Ikea refused fixture construction placement");
            std::vector<ikea::journal::entry> records(*capacity);
            ikea::journal effects{records};
            check(bool(sp::initialize(target, std::span<const u64>(streams[k]), effects)), "Ikea refused fixture values");
            // This fixture never implements a leaf codec: check its construction
            // through Ikea's separately compiled reader before using the image.
            const auto reader = sp::bind_reader<u64>({k, geometry, body, streams[k].size()});
            check(bool(reader), "Ikea refused initialized fixture");
            for (std::size_t r = 0; r < streams[k].size(); ++r)
                check(reader->get_unchecked(r) == streams[k][r], "fixture round trip mismatch");
        }
        out.array_header[k] = out.bytes.size();
        put<std::uint8_t>(out.bytes, k);
        put<std::uint32_t>(out.bytes, counts[k]);
        put<std::uint32_t>(out.bytes, body.size());
        out.bytes.insert(out.bytes.end(), body.begin(), body.end());
    }
    return out;
}
std::vector<block> blocks_for(unsigned width, unsigned rows, bool exceptions) {
    std::vector<block> blocks;
    for (unsigned at = 0; at < rows; at += 256)
        blocks.push_back(make_block(width, std::min(256u, rows - at), exceptions));
    return blocks;
}

void positive_matrix(guarded &guard) {
    for (auto p : {fo::image_preset::compact, fo::image_preset::bulk_x86, fo::image_preset::bulk_arm})
        for (unsigned width = 0; width <= 64; ++width)
            for (unsigned rows : {1u, 255u, 256u, 257u, 4096u})
                for (bool bounded : {false, true}) {
                    auto blocks = blocks_for(width, rows, bounded);
                    const auto image = encode(blocks, p, bounded);
                    const auto admitted = fo::validate_group(guard.place(image.bytes), p, bounded, rows, 0);
                    check(bool(admitted), "valid initialized image refused");
                    check(admitted->blocks == blocks.size() && admitted->rows[blocks.size() - 1] == blocks.back().fields.size(),
                        "admitted logical extent changed");
                    ++valid_cases;
                }
    for (auto [rows, group] : {std::pair{4097u, 1u}, std::pair{65535u, 15u}, std::pair{65536u, 15u}}) {
        const auto n = std::min(4096u, rows - group * 4096);
        const auto blocks = blocks_for(13, n, true);
        const auto image = encode(blocks, fo::image_preset::bulk_x86, true);
        check(bool(fo::validate_group(guard.place(image.bytes), fo::image_preset::bulk_x86, true, rows, group)),
            "last/partial group geometry failed");
        ++valid_cases;
    }
}

void negatives(guarded &guard) {
    auto blocks = blocks_for(13, 513, true);
    const auto image = encode(blocks, fo::image_preset::compact, true);
    auto admit = [&](const std::vector<std::byte> &bytes) {
        return fo::validate_group(guard.place(bytes), fo::image_preset::compact, true, 513, 0);
    };
    for (std::size_t n = 0; n < image.bytes.size(); ++n) {
        const auto truncated = std::span(image.bytes).first(n);
        check(!fo::validate_group(guard.place(truncated), fo::image_preset::compact, true, 513, 0),
            "truncated image was accepted");
        ++truncated_cases;
    }
    auto corrupt = [&](auto change, fo::image_error expected) {
        auto bytes = image.bytes;
        change(bytes);
        const auto got = admit(bytes);
        check(!got && got.error() == expected, "corruption was accepted or misclassified");
        ++malformed_cases;
    };
    corrupt([](auto &b) { b.push_back(std::byte{0}); }, fo::image_error::trailing);
    corrupt([](auto &b) { b[8] = std::byte{65}; }, fo::image_error::descriptor);
    corrupt([](auto &b) { overwrite<std::uint16_t>(b, 11, 257); }, fo::image_error::descriptor);
    corrupt([](auto &b) { overwrite<std::uint16_t>(b, 9, 1); }, fo::image_error::descriptor);
    corrupt([&](auto &b) { overwrite<std::uint16_t>(b, image.capacity_offset, 65535); }, fo::image_error::truncated);
    corrupt([&](auto &b) { overwrite<std::uint16_t>(b, image.capacity_offset + 2, 65535); }, fo::image_error::membership);
    corrupt([&](auto &b) { b[image.positions + 1] = b[image.positions]; }, fo::image_error::membership);
    corrupt([&](auto &b) { b[image.positions + 6] = std::byte{1}; }, fo::image_error::membership); // one-row final block
    corrupt([&](auto &b) { b[image.leaf] = std::byte{12}; }, fo::image_error::descriptor);
    corrupt([&](auto &b) { b[image.leaf + 1] = std::byte{1}; }, fo::image_error::membership);
    corrupt([&](auto &b) { b[image.leaf + 5] = std::byte{0}; }, fo::image_error::membership);
    corrupt([&](auto &b) { overwrite<std::uint16_t>(b, image.leaf + 2, 255); }, fo::image_error::descriptor);
    corrupt([&](auto &b) { b[image.array_count] = std::byte{0}; }, fo::image_error::membership);
    corrupt([&](auto &b) { b[image.array_header[13]] = std::byte{65}; }, fo::image_error::membership);
    corrupt([&](auto &b) { overwrite<std::uint32_t>(b, image.array_header[13] + 1, ~std::uint32_t{0}); }, fo::image_error::leaf);
    corrupt([&](auto &b) { overwrite<std::uint32_t>(b, image.array_header[13] + 5, ~std::uint32_t{0}); }, fo::image_error::leaf);

    blocks[0].fields[0] = 1;  // Individually valid child, inconsistent with the parent's exception sentinel.
    const auto inconsistent = fo::validate_group(guard.place(encode(blocks, fo::image_preset::compact, true).bytes),
        fo::image_preset::compact, true, 513, 0);
    check(!inconsistent && inconsistent.error() == fo::image_error::contents,
        "exception with a nonzero child field was admitted");
    ++malformed_cases;
    auto overflowing = blocks_for(64, 256, false);
    overflowing[0].lo = 1;
    overflowing[0].fields[255] = ~u64{0};
    const auto overflow_image = encode(overflowing, fo::image_preset::compact, true);
    const auto overflow_result = fo::validate_group(guard.place(overflow_image.bytes), fo::image_preset::compact, true, 256, 0);
    check(!overflow_result && overflow_result.error() == fo::image_error::contents, "base plus field overflow admitted");
    ++malformed_cases;

    auto mixed = blocks_for(7, 513, false);
    mixed[1] = make_block(13, 256, false), mixed[2] = make_block(0, 1, false);
    auto mixed_image = encode(mixed, fo::image_preset::bulk_arm, false);
    mixed_image.bytes[mixed_image.array_header[13]] = std::byte{7};
    check(!fo::validate_group(guard.place(mixed_image.bytes), fo::image_preset::bulk_arm, false, 513, 0),
        "duplicate/out-of-order width array admitted");
    ++malformed_cases;

    const auto unreadable = std::span<const std::byte>(guard.inaccessible(), 1);
    for (auto [rows, group] : {std::pair{0u, 0u}, std::pair{65537u, 0u}, std::pair{4096u, 1u},
                              std::pair{4097u, 2u}, std::pair{65536u, ~0u}}) {
        check(!fo::validate_group(unreadable, fo::image_preset::compact, true, rows, group),
            "invalid/absent group admitted");
        ++malformed_cases;
    }
    check(!fo::validate_group(unreadable, fo::image_preset::compact, true, ~std::size_t{0}, 0),
        "overflowing logical extent acquired bytes");
    ++malformed_cases;
    const auto unknown = fo::validate_group(guard.place(image.bytes), static_cast<fo::image_preset>(255), true, 513, 0);
    check(!unknown, "unknown preset silently interpreted as compact");
    ++malformed_cases;
}
}  // namespace

int main() {
    guarded guard(65536);
    positive_matrix(guard);
    negatives(guard);
    std::printf("Frame group images: %zu valid, %zu guarded truncated prefixes, %zu malformed/invalid cases pass; Ikea-initialized leaves, all widths/presets\n",
        valid_cases, truncated_cases, malformed_cases);
}
