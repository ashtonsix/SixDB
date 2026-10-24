#include "cells.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <limits>
#include <random>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace fc = frame_cells;
using fc::u64;
using fc::u128;
namespace {
std::size_t checks = 0, refusals = 0, protected_cases = 0;
void require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "native controls: %s\n", what); std::abort(); }
}
template<class F> void refuses(F&& f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "expected domain refusal");
    ++refusals;
}
bool selected(std::span<const u64> words, std::size_t row) {
    return (words[row / 64] >> (row % 64)) & 1;
}
fc::answer sum_oracle(std::span<const u64> values, std::span<const u64> coarse, std::span<const u64> fine) {
    fc::answer out;
    for (std::size_t row = 0; row < values.size(); ++row)
        if (selected(coarse, row / 256) && selected(fine, row)) {
            out.sum += u128{values[row]}; ++out.count;
        }
    return out;
}
fc::answer points_oracle(std::span<const u64> values, std::span<const u64> coarse, std::span<const std::size_t> rows) {
    fc::answer out;
    for (const auto row : rows)
        if (selected(coarse, row / 256)) { out.sum += u128{values[row]}; ++out.count; }
    return out;
}

void fixture(std::vector<u64> values, unsigned bits, std::mt19937_64& rng) {
    fc::native payload(values);
    const auto rows = values.size(), blocks = rows / 256;
    const auto expected_lane = bits <= 8 ? 1u : bits <= 16 ? 2u : bits <= 32 ? 4u : 8u;
    require(payload.lane() == expected_lane, "fixture did not reach intended native lane");
    std::vector<std::size_t> requests;
    if (rows) {
        requests = {rows - 1, 0, 63, 64, 255, 256, rows - 1, 0, 256};
        for (unsigned i = 0; i < 1027; ++i) requests.push_back(rng() % rows);
    }
    const auto admitted = fc::point_requests::bind(requests, rows);
    const std::array<u64, 8> thresholds{0, 1, payload.base, payload.maximum,
        payload.base == ~u64{0} ? payload.base : payload.base + 1,
        payload.maximum == ~u64{0} ? payload.maximum : payload.maximum + 1,
        ~u64{0}, rows ? values[rows / 2] : 0};
    for (unsigned shape = 0; shape < 5; ++shape) {
        std::vector<u64> coarse((blocks + 63) / 64);
        for (std::size_t b = 0; b < blocks; ++b)
            if (shape == 0 || (shape == 2 && b % 2 == 0) || (shape == 3 && b == 0) || (shape == 4 && b + 1 == blocks))
                coarse[b / 64] |= u64{1} << (b % 64);
        const auto domain = fc::block_mask::bind(coarse, rows);
        fc::validate_native_domains(payload, domain);
        for (auto t : thresholds) {
            u64 expected = 0;
            for (std::size_t row = 0; row < rows; ++row)
                expected += selected(coarse, row / 256) && values[row] >= t;
            require(fc::native_count_once(payload, t, domain) == expected, "standalone COUNT");
        }
        fc::validate_native_points(payload, domain, admitted);
        require(fc::native_points_once(payload, domain, admitted) == points_oracle(values, coarse, requests), "independent points");
        auto reversed = requests;
        std::reverse(reversed.begin(), reversed.end());
        const auto reverse_view = fc::point_requests::bind(reversed, rows);
        require(fc::native_points_once(payload, domain, reverse_view) == points_oracle(values, coarse, reversed), "reversed duplicate points");
        for (unsigned pattern = 0; pattern < 7; ++pattern) {
            std::vector<u64> fine(rows / 64);
            for (std::size_t w = 0; w < fine.size(); ++w) {
                if (pattern == 1) fine[w] = ~u64{0};
                else if (pattern == 2) fine[w] = 0xaaaaaaaaaaaaaaaaull;
                else if (pattern == 3) fine[w] = (u64{1} << 63) | 1;
                else if (pattern == 4) fine[w] = w % 4 == 0 ? ~u64{0} : 0;
                else if (pattern == 5) fine[w] = rng();
                else if (pattern == 6) fine[w] = u64{1} << (rng() % 64);
            }
            const auto mask = fc::row_mask::bind(fine, rows);
            fc::validate_native_selection(payload, domain, mask);
            const auto expected = sum_oracle(values, coarse, fine);
            require(fc::native_selected_sum_once(payload, domain, mask) == expected, "sparse supplied-mask SUM");
            require(fc::native_selected_sum_dense_once(payload, domain, mask) == expected, "dense supplied-mask SUM");
            if (pattern == 1) require(fc::native_sum_once(payload, domain) == expected, "existing whole SUM regression");
        }
    }
}

struct protected_page {
    void* address;
    std::size_t bytes;
    protected_page(void* begin, std::size_t length) {
        bytes = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        const auto first = reinterpret_cast<std::uintptr_t>(begin);
        const auto aligned = (first + bytes - 1) / bytes * bytes;
        require(aligned + bytes <= first + length, "owned vector lacks a complete interior page");
        address = reinterpret_cast<void*>(aligned);
    }
    void deny() const { if (mprotect(address, bytes, PROT_NONE)) std::abort(); }
    void allow() const { if (mprotect(address, bytes, PROT_READ | PROT_WRITE)) std::abort(); }
};
template<class F> void protected_child(F&& f, bool must_fault = false) {
    const auto child = fork();
    require(child >= 0, "fork");
    if (!child) {
        if (must_fault) {
            const rlimit limit{0, 0};
            if (setrlimit(RLIMIT_CORE, &limit)) _exit(99);
            close(STDERR_FILENO);
        }
        f(); _exit(0);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child, "waitpid");
    const bool normal = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    const bool faulted = (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV) ||
                         (WIFEXITED(status) && WEXITSTATUS(status) == 1); // ASan's fatal-access exit.
    require(must_fault ? faulted : normal, must_fault ? "protected positive read did not fault" : "empty/excluded payload was accessed");
    ++protected_cases;
}

void access_checks() {
    constexpr std::size_t rows = 65536;
    std::vector<u64> original(rows);
    for (std::size_t i = 0; i < rows; ++i) original[i] = i & 255;
    fc::native payload(original);
    auto& stored = std::get<std::vector<fc::u8>>(payload.values);
    protected_page page(stored.data(), stored.size());
    std::vector<u64> coarse(rows / 256 / 64), fine(rows / 64);
    std::vector<std::size_t> requests(2048, 0);
    const auto none = fc::block_mask::bind(coarse, rows);
    const auto empty = fc::row_mask::bind(fine, rows);
    const auto points = fc::point_requests::bind(requests, rows);
    fc::validate_native_selection(payload, none, empty);
    fc::validate_native_points(payload, none, points);
    const auto before = stored;
    protected_child([&] {
        page.deny();
        require(fc::native_count_once(payload, 128, none) == 0, "coarse-none COUNT");
        require(fc::native_sum_once(payload, none) == fc::answer{}, "coarse-none whole SUM");
        require(fc::native_selected_sum_once(payload, none, empty) == fc::answer{}, "coarse-none selected SUM");
        require(fc::native_selected_sum_dense_once(payload, none, empty) == fc::answer{}, "coarse-none dense SUM");
        require(fc::native_points_once(payload, none, points) == fc::answer{}, "coarse-none points");
        page.allow();
    });
#if __has_feature(address_sanitizer)
    // Unlike the interior guard page, poisoning covers the complete owned
    // payload and catches a stray first-element load on an empty domain.
    __asan_poison_memory_region(stored.data(), stored.size());
    __asan_poison_memory_region(fine.data(), fine.size() * sizeof(u64));
    __asan_poison_memory_region(requests.data(), requests.size() * sizeof(std::size_t));
    require(fc::native_count_once(payload, 128, none) == 0, "poisoned coarse-none COUNT");
    require(fc::native_sum_once(payload, none) == fc::answer{}, "poisoned coarse-none SUM");
    require(fc::native_selected_sum_once(payload, none, empty) == fc::answer{}, "poisoned coarse-none sparse SUM");
    require(fc::native_selected_sum_dense_once(payload, none, empty) == fc::answer{}, "poisoned coarse-none dense SUM");
    require(fc::native_points_once(payload, none, points) == fc::answer{}, "poisoned coarse-none points");
    __asan_unpoison_memory_region(stored.data(), stored.size());
    __asan_unpoison_memory_region(fine.data(), fine.size() * sizeof(u64));
    __asan_unpoison_memory_region(requests.data(), requests.size() * sizeof(std::size_t));
#endif
    const std::vector<u64> all_words(coarse.size(), ~u64{0});
    const auto all = fc::block_mask::bind(all_words, rows);
    protected_child([&] {
        page.deny();
        require(fc::native_selected_sum_once(payload, all, empty) == fc::answer{}, "fine-none SUM");
        require(fc::native_selected_sum_dense_once(payload, all, empty) == fc::answer{}, "fine-none dense SUM");
        page.allow();
    });
    protected_child([&] { page.deny(); volatile auto observed = fc::native_count_once(payload, 128, all); (void)observed; }, true);
    const auto protected_first = reinterpret_cast<std::uintptr_t>(page.address) - reinterpret_cast<std::uintptr_t>(stored.data());
    const auto protected_end = protected_first + page.bytes;
    coarse = all_words;
    for (std::size_t b = 0; b < rows / 256; ++b)
        if (b * 256 < protected_end && (b + 1) * 256 > protected_first)
            coarse[b / 64] &= ~(u64{1} << (b % 64));
    const auto excluded = fc::block_mask::bind(coarse, rows);
    std::fill(fine.begin(), fine.end(), ~u64{0});
    const auto full = fc::row_mask::bind(fine, rows);
    for (std::size_t i = 0; i < requests.size(); ++i) requests[i] = i % 2 ? protected_first : rows - 1;
    const auto mixed = fc::point_requests::bind(requests, rows);
    const auto expected = sum_oracle(original, coarse, fine);
    const auto expected_points = points_oracle(original, coarse, requests);
    u64 expected_count = 0;
    for (std::size_t i = 0; i < rows; ++i) expected_count += selected(coarse, i / 256) && original[i] >= 128;
    protected_child([&] {
        page.deny();
        require(fc::native_count_once(payload, 128, excluded) == expected_count, "excluded COUNT");
        require(fc::native_selected_sum_once(payload, excluded, full) == expected, "excluded selected SUM");
        require(fc::native_selected_sum_dense_once(payload, excluded, full) == expected, "excluded dense SUM");
        require(fc::native_points_once(payload, excluded, mixed) == expected_points, "excluded requested points");
        page.allow();
    });
    for (std::size_t w = 0; w < fine.size(); ++w)
        if (w * 64 < protected_end && (w + 1) * 64 > protected_first) fine[w] = 0;
    const auto empty_words = fc::row_mask::bind(fine, rows);
    const auto expected_words = sum_oracle(original, all_words, fine);
    protected_child([&] {
        page.deny();
        require(fc::native_selected_sum_once(payload, all, empty_words) == expected_words, "empty words sparse SUM");
        require(fc::native_selected_sum_dense_once(payload, all, empty_words) == expected_words, "empty words dense SUM");
        page.allow();
    });
    require(stored == before, "query changed source bytes");
}

void domain_checks() {
    fc::native payload(std::vector<u64>(512, 17));
    const std::array<u64, 1> coarse{3};
    const auto domain = fc::block_mask::bind(coarse, 512);
    const std::array<u64, 8> fine{};
    const auto mask = fc::row_mask::bind(fine, 512);
    const std::array<std::size_t, 3> rows{0, 511, 0};
    const auto points = fc::point_requests::bind(rows, 512);
    fc::validate_native_selection(payload, domain, mask);
    fc::validate_native_points(payload, domain, points);
    const auto empty_points = fc::point_requests::bind({}, payload.rows);
    require(fc::native_points_once(payload, domain, empty_points) == fc::answer{}, "empty request list");
    refuses([&] { (void)fc::row_mask::bind(fine, 513); });
    const std::array<u64, 1> outside{u64{1} << 3};
    refuses([&] { (void)fc::row_mask::bind(outside, 3); });
    const std::array<std::size_t, 1> outside_row{512};
    refuses([&] { (void)fc::point_requests::bind(outside_row, 512); });
    refuses([&] { (void)fc::point_requests::bind(outside_row, 0); });
    const std::array<u64, 4> short_fine{};
    const auto short_mask = fc::row_mask::bind(short_fine, 256);
    refuses([&] { fc::validate_native_selection(payload, domain, short_mask); });
    const auto short_points = fc::point_requests::bind({}, 256);
    refuses([&] { fc::validate_native_points(payload, domain, short_points); });
    const auto short_domain = fc::block_mask::bind(coarse, 256);
    refuses([&] { fc::validate_native_selection(payload, short_domain, mask); });
    const std::array<u64, 1> partial{7};
    const auto partial_mask = fc::row_mask::bind(partial, 3);
    require(partial_mask.all && !partial_mask.none, "partial mask admission");
}
} // namespace

int main() {
    std::mt19937_64 rng(991);
    fixture({}, 0, rng);
    for (unsigned bits = 0; bits <= 64; ++bits) {
        const auto maximum = bits == 64 ? ~u64{0} : (u64{1} << bits) - 1;
        const auto base = (~maximum) & 0xfedcba9876543210ull;
        std::vector<u64> values(3 * 256);
        for (auto& value : values) value = base + (rng() & maximum);
        values.front() = base; values.back() = base + maximum;
        fixture(std::move(values), bits, rng);
    }
    fixture(std::vector<u64>(131328, ~u64{0}), 0, rng);
    // More requests than the half-sum chunk, including full-width carry cases.
    const std::vector<u64> extremes{0, ~u64{0}};
    std::vector<u64> values(512);
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = extremes[i % 2];
    fc::native payload(values);
    const std::array<u64, 1> coarse{3};
    const auto domain = fc::block_mask::bind(coarse, values.size());
    std::vector<std::size_t> requests(131077);
    for (std::size_t i = 0; i < requests.size(); ++i) requests[i] = (i * 17 + 1) % values.size();
    const auto points = fc::point_requests::bind(requests, values.size());
    require(fc::native_points_once(payload, domain, points) == points_oracle(values, coarse, requests), "wide point chunk carries");
    domain_checks();
    access_checks();
    std::printf("{\"checks\":%zu,\"refusals\":%zu,\"protected_cases\":%zu,\"pass\":true}\n", checks, refusals, protected_cases);
}
