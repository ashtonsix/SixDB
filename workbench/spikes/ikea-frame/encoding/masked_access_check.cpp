// Private research fixture: reuse the real body without creating a public API.
// Page-per-block placement is only for access checking, never for timing.
#define FRAME_ENCODING_BODY_NO_MAIN
#include "bodies.cpp"

#include <csignal>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void need(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "masked access check: %s\n", message);
        std::abort();
    }
}

struct pages {
    std::size_t page, count;
    std::uint8_t *data;
    explicit pages(std::size_t n) : page(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))), count(n) {
        need(page >= block_rows && page <= (1u << 20), "invalid page size");
        void *mapping = ::mmap(nullptr, page * count, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        need(mapping != MAP_FAILED, "mmap failed");
        data = static_cast<std::uint8_t *>(mapping);
    }
    pages(const pages &) = delete;
    ~pages() { ::munmap(data, page * count); }
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
    pages payload{count}, owner{1};
    column *material;
    fixture() {
        need(sizeof(column) <= owner.page, "column exceeds owner page");
        material = std::construct_at(reinterpret_cast<column *>(owner.data));
        material->rows = count * block_rows;
        for (std::size_t b = 0; b < count; ++b) {
            auto *bytes = payload.data + b * payload.page;
            std::memset(bytes, static_cast<int>(b + 1), block_rows);
            const sp::dense_input input{8, sp::geometry::local,
                                       std::span<const std::uint8_t>(bytes, block_rows), block_rows};
            auto wide = sp::bind_decoder<u64>(input);
            auto narrow = sp::bind_decoder<std::uint8_t>(input);
            need(wide.has_value() && narrow.has_value(), "decoder binding failed");
            material->blocks.push_back({0, 0, block_rows, kind::packed, 8});
            material->decoders.push_back(*wide);
            material->d8.emplace_back(*narrow);
            material->d16.emplace_back();
            material->d32.emplace_back();
            material->runs.emplace_back();
        }
    }
    ~fixture() {
        owner.all(PROT_READ | PROT_WRITE);
        std::destroy_at(material);
    }
};

void verify(const column &c, const block_mask &mask, u128 sum, u64 count) {
    need(encoded_sum(c, mask) == sum, "SUM differs from literal expected values");
    need(encoded_sum_narrow(c, mask) == sum, "narrow SUM differs from literal expected values");
    need(encoded_count_ge(c, mask, 25) == count, "COUNT differs from literal expected values");
}

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

    f.payload.all(PROT_NONE);
    f.owner.all(PROT_NONE);
    verify(*f.material, none, 0, 0); // Even the column object is inaccessible.
    f.owner.all(PROT_READ | PROT_WRITE);

    for (std::size_t b = 16; b < 32; ++b) f.payload.one(b, PROT_READ);
    for (auto b : {32u, 34u, 36u}) f.payload.one(b, PROT_READ);
    verify(*f.material, selected, 127232, 2816);

    block_mask broken(fixture::count);
    broken.set(0); // Group 0 remains inaccessible; each operation must fault.
    must_fault([&] { return encoded_sum(*f.material, broken); });
    must_fault([&] { return encoded_sum_narrow(*f.material, broken); });
    must_fault([&] { return static_cast<u128>(encoded_count_ge(*f.material, broken, 25)); });
    std::puts("Protected owner/payload checks passed: none, skipped/full/mixed groups, partial tail; "
              "all three injected inactive reads faulted.");
}
