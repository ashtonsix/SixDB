// Private research fixture: reuse the real body without creating a public API.
// Page-per-block placement is only for access checking, never for timing.
#include "material.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include <csignal>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace frame_encoding;

void need(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "masked access check: %s\n", message);
        std::abort();
    }
}

struct pages {
    std::size_t page, count;
    std::uint8_t *data;
    bool owned = true;
    explicit pages(std::size_t n) : page(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))), count(n) {
        need(page >= block_rows && page <= (1u << 20), "invalid page size");
        void *mapping = ::mmap(nullptr, page * count, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        need(mapping != MAP_FAILED, "mmap failed");
        data = static_cast<std::uint8_t *>(mapping);
    }
    pages(std::vector<std::uint8_t> &storage, std::size_t n)
        : page(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))), count(n), owned(false) {
        need(page >= block_rows && page <= (1u << 20), "invalid page size");
        storage.resize((count + 2) * page);
        const auto address = reinterpret_cast<std::uintptr_t>(storage.data());
        const auto padding = (page - address % page) % page;
        data = storage.data() + padding;
        need(padding + count * page <= storage.size(), "protected pages exceed payload");
    }
    pages(const pages &) = delete;
    ~pages() { if (owned) ::munmap(data, page * count); }
    void all(int permissions) {
        need(::mprotect(data, page * count, permissions) == 0, "mprotect all failed");
    }
    void one(std::size_t n, int permissions) {
        need(n < count && ::mprotect(data + n * page, page, permissions) == 0,
             "mprotect page failed");
    }
};

// Each packed block contains 256 copies of b+1, through real Local K=8
// decoder bindings. The descriptor deliberately stays packed, so thresholds
// between 1 and 255 must access the selected payload rather than constant-fold.
struct fixture {
    static constexpr std::size_t count = 37; // Empty, full and partial/mixed groups.
    pages owner{1};
    column *material;
    std::unique_ptr<pages> payload;
    fixture() {
        need(sizeof(column) <= owner.page, "column exceeds owner page");
        material = std::construct_at(reinterpret_cast<column *>(owner.data));
        material->rows = count * block_rows;
        payload = std::make_unique<pages>(material->payload, count);
        for (std::size_t b = 0; b < count; ++b) {
            auto *bytes = payload->data + b * payload->page;
            std::memset(bytes, static_cast<int>(b + 1), block_rows);
            const sp::dense_input input{8, sp::geometry::local,
                                       std::span<const std::uint8_t>(bytes, block_rows), block_rows};
            auto wide = sp::bind_decoder<u64>(input);
            auto narrow = sp::bind_decoder<std::uint8_t>(input);
            need(wide.has_value() && narrow.has_value(), "decoder binding failed");
            const auto offset = bytes - material->payload.data();
            need(offset >= 0 && static_cast<std::size_t>(offset) <= UINT32_MAX, "offset overflow");
            material->blocks.push_back({0, static_cast<std::uint32_t>(offset), block_rows, kind::packed, 8});
            material->decoders.push_back(*wide);
            material->d8.emplace_back(*narrow);
            material->d16.emplace_back();
            material->d32.emplace_back();
        }
    }
    ~fixture() {
        owner.all(PROT_READ | PROT_WRITE);
        payload->all(PROT_READ | PROT_WRITE);
        std::destroy_at(material);
    }
};

void verify(const column &c, const block_mask &mask, u128 sum, u64 count, u64 threshold = 25) {
    need(encoded_sum(c, mask) == sum, "SUM differs from literal expected values");
    need(encoded_sum_narrow(c, mask) == sum, "narrow SUM differs from literal expected values");
    need(encoded_count_ge(c, mask, threshold) == count, "COUNT differs from literal expected values");
    need(catalogue_sum(c, mask) == sum, "catalogue SUM differs from literal expected values");
    need(catalogue_count_ge(c, mask, threshold) == count, "catalogue COUNT differs from literal expected values");
}

struct dictionary_fixture {
    static constexpr std::size_t count = 258; // Two editions, final partial group.
    pages owner{1};
    column *material;
    std::unique_ptr<pages> payload;
    dictionary_fixture() {
        material = std::construct_at(reinterpret_cast<column *>(owner.data));
        material->options.dictionary = dictionary_order::sorted;
        material->rows = count * block_rows;
        payload = std::make_unique<pages>(material->payload, count + 2);
        const auto offset = [&](const std::uint8_t *p) {
            return static_cast<std::uint32_t>(p - material->payload.data());
        };
        material->coded.resize((count + 63) / 64);
        for (unsigned b = 0; b < count; ++b) {
            auto *data = payload->data + b * payload->page;
            std::memset(data, b % 2, block_rows);
            material->blocks.push_back({0, offset(data), block_rows, kind::packed, 8});
            const auto decoder = sp::bind_decoder<u64>(sp::dense_input{
                8, sp::geometry::local, std::span<const std::uint8_t>(data, block_rows), block_rows});
            need(decoder.has_value(), "code plane binding failed");
            material->decoders.push_back(*decoder);
            material->coded[b / 64] |= u64{1} << (b % 64);
        }
        for (unsigned p = 0; p < 2; ++p) {
            auto *data = payload->data + (count + p) * payload->page;
            data[0] = 7; data[1] = 19;
            material->parts.push_back({1000u * (p + 1), offset(data), 2, 8});
            const auto decoder = sp::bind_decoder<u64>(sp::dense_input{
                8, sp::geometry::local, std::span<const std::uint8_t>(data, 8), 2});
            need(decoder.has_value(), "dictionary plane binding failed");
            material->dictionaries.emplace_back(*decoder);
        }
    }
    ~dictionary_fixture() {
        owner.all(PROT_READ | PROT_WRITE);
        payload->all(PROT_READ | PROT_WRITE);
        std::destroy_at(material);
    }
};

template <class F> void must_fault(F &&read) {
    const auto child = ::fork();
    need(child >= 0, "fork failed");
    if (child == 0) {
        // A deliberate broken selection must be observable, without core files.
        const rlimit no_core{0, 0};
        if (::setrlimit(RLIMIT_CORE, &no_core) != 0) ::_exit(120);
        consume(read());
        ::_exit(0);
    }
    int status = 0;
    need(::waitpid(child, &status, 0) == child, "waitpid failed");
    need(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS),
         "injected inactive read was not detected by the protected payload");
}
} // namespace

int main() {
    fixture f;
    block_mask all(fixture::count), none(fixture::count), selected(fixture::count);
    for (std::size_t b = 0; b < fixture::count; ++b) all.set(b);
    for (std::size_t b = 16; b < 32; ++b) selected.set(b);
    for (auto b : {32u, 34u, 36u}) selected.set(b);

    // Literal arithmetic over 1..37 and selected 17..32,33,35,37. Every
    // selected block contains 256 equal values; threshold is 25.
    verify(*f.material, all, 179968, 3328);

    f.payload->all(PROT_NONE);
    f.owner.all(PROT_NONE);
    verify(*f.material, none, 0, 0); // Even the column object is inaccessible.
    f.owner.all(PROT_READ | PROT_WRITE);

    for (std::size_t b = 16; b < 32; ++b) f.payload->one(b, PROT_READ);
    for (auto b : {32u, 34u, 36u}) f.payload->one(b, PROT_READ);
    verify(*f.material, selected, 127232, 2816);

    block_mask broken(fixture::count);
    broken.set(0); // Group 0 remains inaccessible; each operation must fault.
    must_fault([&] { return encoded_sum(*f.material, broken); });
    must_fault([&] { return encoded_sum_narrow(*f.material, broken); });
    must_fault([&] { return static_cast<u128>(encoded_count_ge(*f.material, broken, 25)); });
    must_fault([&] { return catalogue_sum(*f.material, broken); });
    must_fault([&] { return static_cast<u128>(catalogue_count_ge(*f.material, broken, 25)); });

    dictionary_fixture dictionaries;
    block_mask none_codes(dictionary_fixture::count), first_part(dictionary_fixture::count);
    block_mask second_part(dictionary_fixture::count);
    for (auto b : {0u, 17u, 63u, 64u, 255u}) first_part.set(b);
    second_part.set(256); second_part.set(257);
    dictionaries.payload->all(PROT_NONE);
    dictionaries.owner.all(PROT_NONE);
    verify(*dictionaries.material, none_codes, 0, 0, 1010);
    dictionaries.owner.all(PROT_READ | PROT_WRITE);
    for (auto b : {0u, 17u, 63u, 64u, 255u, 258u}) dictionaries.payload->one(b, PROT_READ);
    // Five literal blocks: two at1007, three at1019. The other dictionary and
    // every excluded block remain inaccessible, including the following part.
    verify(*dictionaries.material, first_part, 1298176, 768, 1010);
    dictionaries.payload->all(PROT_NONE);
    for (auto b : {256u, 257u, 259u}) dictionaries.payload->one(b, PROT_READ);
    verify(*dictionaries.material, second_part, 1030656, 512, 1010);
    dictionaries.payload->all(PROT_NONE);
    for (auto b : {0u, 17u, 63u, 64u, 255u}) dictionaries.payload->one(b, PROT_READ);
    // A selected coded block genuinely requires its own edition. Removing that
    // one dictionary must fault, rather than borrowing the neighboring edition.
    must_fault([&] { return encoded_sum(*dictionaries.material, first_part); });
    must_fault([&] { return encoded_sum_narrow(*dictionaries.material, first_part); });
    must_fault([&] { return static_cast<u128>(encoded_count_ge(*dictionaries.material, first_part, 1010)); });
    must_fault([&] { return catalogue_sum(*dictionaries.material, first_part); });
    must_fault([&] { return static_cast<u128>(catalogue_count_ge(*dictionaries.material, first_part, 1010)); });
    std::puts("Protected owner/payload checks passed: none, skipped/full/mixed groups, partial tail; "
              "dictionary editions outside the selected body; all ten injected reads faulted.");
}
