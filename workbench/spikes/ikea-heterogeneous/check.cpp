#include "bec_region.h"
#include "fixture.h"
#include "metadata_native.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
using namespace ikea::heterogeneous;
static void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::abort();
  }
}
static EntryLanes16 read_native(MetadataKind kind, const std::uint8_t *data,
                                unsigned capacity, unsigned first) {
  switch (kind) {
  case MetadataKind::direct32:
    return read_metadata16<MetadataKind::direct32>(data, capacity, first);
  case MetadataKind::local16:
    return read_metadata16<MetadataKind::local16>(data, capacity, first);
  case MetadataKind::scan128:
    return read_metadata16<MetadataKind::scan128>(data, capacity, first);
  }
  std::abort();
}
int main() {
  require(check_bec_regions() == 0, "native BEC regions");
  std::uint64_t ranges = 0, entries = 0;
  const auto page = std::size_t(sysconf(_SC_PAGESIZE));
  auto *guard = static_cast<std::uint8_t *>(
      mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  require(guard != MAP_FAILED, "mmap");
  require(mprotect(guard + page, page, PROT_READ | PROT_WRITE) == 0,
          "mprotect");
  for (unsigned count : {0u, 1u, 2u, 3u, 15u, 16u, 17u, 31u, 32u, 33u, 127u,
                         128u, 129u, 255u, 256u}) {
    for (unsigned seed : {0u, 1u, 128u, 255u, 256u}) {
      auto plain = structural_blocks(count, seed);
      auto source = encode_source(plain);
      auto query = make_query(count, seed + 123);
      for (auto kind : {MetadataKind::direct32, MetadataKind::local16,
                        MetadataKind::scan128}) {
        auto metadata = std::make_shared<MetadataOwner>(source, kind);
        const auto size = metadata->bytes().size();
        const auto capacity = metadata->capacity();
        for (unsigned group = 0; group < capacity; group += 16) {
          std::uint32_t output[16];
          const auto frame =
              read_native(kind, metadata->bytes().data(), capacity, group);
          store_metadata16(output, frame);
          for (unsigned lane = 0; lane < 16; ++lane) {
            const auto e = read_entry_reference(
                kind, {metadata->bytes().data(), size}, capacity, group + lane);
            const auto expected = (unsigned(e.offset) << 16) | e.population;
            require(output[lane] == expected &&
                        metadata_entry_at(frame, lane) == expected,
                    "native metadata");
            if (group + lane < count)
              require(e == source.entries()[group + lane],
                      "metadata association");
            ++entries;
          }
        }
        // Test first/end exact allocation boundaries, with all unreachable
        // padding poisoned under ASan. Metadata base remains64B aligned.
        for (auto *address : {guard + page, guard + 2 * page - size}) {
          if (size)
            std::memcpy(address, metadata->bytes().data(), size);
#if __has_feature(address_sanitizer)
          __asan_poison_memory_region(guard + page, page);
          if (size)
            __asan_unpoison_memory_region(address, size);
#endif
          for (unsigned group = 0; group < capacity; group += 16) {
            std::uint32_t output[16];
            store_metadata16(output,
                             read_native(kind, address, capacity, group));
            require((output[0] & 65535) <= 256, "guard metadata value");
          }
#if __has_feature(address_sanitizer)
          __asan_unpoison_memory_region(guard + page, page);
#endif
        }
        PreparedRange full;
        require(prepare(metadata, source.body(), query, 0, count,
                        Execution::split, full) == AdmissionError::none,
                "prepare valid source");
        require(full.count() == reference_count(plain, *query, 0, count),
                "prepared full range");
        const RangeKernel kernels[][2] = {
            {ikea_heterogeneous_direct_inline, ikea_heterogeneous_direct_split},
            {ikea_heterogeneous_local_inline, ikea_heterogeneous_local_split},
            {ikea_heterogeneous_scan_inline, ikea_heterogeneous_scan_split}};
        // Every requested range for smaller collections; adversarial range
        // endpoints around all packet boundaries for the larger cases.
        for (unsigned first = 0; first <= count; ++first) {
          for (unsigned n = 0; n <= count - first; ++n) {
            if (count > 33 && !(n <= 3 || n == count - first ||
                                ((first + n) % 16 <= 1 &&
                                 (first % 16 <= 1 || first % 16 == 15))))
              continue;
            auto expected = reference_count(plain, *query, first, n);
            for (auto execution : {Execution::inlined, Execution::split}) {
              require(kernels[unsigned(kind)][unsigned(execution)](
                          metadata->bytes().data(), capacity,
                          source.body()->bytes.data(), query->data(), first,
                          n) == expected,
                      "composed range");
              ++ranges;
            }
          }
        }
#if __has_feature(address_sanitizer)
        if (count > 3) {
          const unsigned first = 3, n = count - 3;
          PreparedRange selected;
          require(prepare(metadata, source.body(), query, first, n,
                          Execution::split, selected) == AdmissionError::none,
                  "query subrange prepare");
          __asan_poison_memory_region(query->data(), first * 32);
          require(selected.count() == reference_count(plain, *query, first, n),
                  "query range accesses");
          __asan_unpoison_memory_region(query->data(), first * 32);
        }
#endif
        PreparedRange invalid;
        auto other = encode_source(plain);
        require(prepare(metadata, other.body(), query, 0, 0, Execution::split,
                        invalid) == AdmissionError::association,
                "foreign body refused");
        require(prepare(metadata, source.body(), query, count + 1, 0,
                        Execution::split, invalid) == AdmissionError::range,
                "bad range refused");
        if (count) {
          auto short_query = std::make_shared<AlignedBytes>(count * 32 - 1);
          require(prepare(metadata, source.body(), short_query, 0, count,
                          Execution::split,
                          invalid) == AdmissionError::query_extent,
                  "short query refused");
        }
        auto body = source.body();
        require(validate_metadata(
                    kind, {metadata->bytes().data(), size}, capacity, count,
                    {body->bytes.data(), body->logical_bytes + 63},
                    body->logical_bytes) == AdmissionError::body_extent,
                "suffix refused");
        if (size) {
          require(validate_metadata(
                      kind, {metadata->bytes().data(), size - 1}, capacity,
                      count, {body->bytes.data(), body->bytes.size()},
                      body->logical_bytes) == AdmissionError::metadata_extent,
                  "short metadata refused");
          std::vector<std::uint8_t> bad(metadata->bytes().data(),
                                        metadata->bytes().data() + size);
          bad[0] ^= 1;
          auto error = validate_metadata(
              kind, bad, capacity, count,
              {body->bytes.data(), body->bytes.size()}, body->logical_bytes);
          // A direct root-population edit may itself describe another valid
          // interpretation; the sealed construction association is separate.
          if (kind != MetadataKind::direct32)
            require(error != AdmissionError::none, "bad checkpoint refused");
        }
      }
    }
  }
  // Prepared reads retain all three immutable resources after their builders
  // die.
  PreparedRange retained;
  std::uint64_t expected;
  {
    auto plain = structural_blocks(17, 240);
    auto source = encode_source(plain);
    auto query = make_query(17, 98);
    auto metadata =
        std::make_shared<MetadataOwner>(source, MetadataKind::local16);
    expected = reference_count(plain, *query, 3, 13);
    require(prepare(metadata, source.body(), query, 3, 13, Execution::split,
                    retained) == AdmissionError::none,
            "retained prepare");
  }
  require(retained.count() == expected, "retained owners");
  munmap(guard, page * 3);
  std::printf("metadata_entries=%llu composed_ranges=%llu; framing, "
              "association, exact metadata, range and lifetime checks passed\n",
              (unsigned long long)entries, (unsigned long long)ranges);
}
