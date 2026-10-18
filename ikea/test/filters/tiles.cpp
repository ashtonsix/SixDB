#include "support.h"

void check_tile_widths_1(std::uint8_t*);
void check_tile_widths_9(std::uint8_t*);
void check_tile_widths_17(std::uint8_t*);
void check_tile_widths_25(std::uint8_t*);
void check_tile_widths_33(std::uint8_t*);
void check_tile_widths_41(std::uint8_t*);
void check_tile_widths_49(std::uint8_t*);
void check_tile_widths_57(std::uint8_t*);

template <class F> void unaligned() {
    constexpr unsigned n = 2 * F::tile_rows;
    constexpr auto limit = ~std::uint64_t{0} >> (64 - F::width);
    constexpr unsigned payload_bytes = 2 * F::tile_bytes;
    alignas(64) std::array<std::uint8_t, 8192> backing{}, heads{}, heads2{};
    std::array<std::uint64_t, n> truth{}, output{};
    for (unsigned i = 0; i < n; ++i)
        truth[i] = (std::uint64_t(i) * 0xe91773403ULL) & limit;
    for (unsigned offset = 0; offset < 64; ++offset) {
        backing.fill(0xcd);
        heads.fill(0xcd);
        heads2.fill(0xcd);
        auto payload = std::span(backing).subspan(offset, payload_bytes);
        auto result = sp::view<F, std::uint8_t>::attach(
            n, {{{payload, F::tile_bytes},
                 {std::span(heads).subspan(offset), F::tile_rows},
                 {std::span(heads2).subspan(offset), F::tile_rows}}});
        assert(result);
        const auto& view = *result;
        const auto prepared = sp::bind_mutation(view);
        assert(prepared);
        std::array<ikea::owner_write, 1024> records;
        ikea::source_write_journal effects{records};
        sp::no_summary summary;
        assert(prepared->replace(0, std::span<const std::uint64_t>(truth), sp::row_selection::all(),
                                 summary, effects));
        auto decoder = sp::bind_decoder<std::uint64_t>(view);
        assert(decoder.read(0, output));
        assert(output == truth);
        for (unsigned i = 0; i < offset; ++i)
            assert(backing[i] == 0xcd);
        for (unsigned i = offset + payload_bytes; i < backing.size(); ++i)
            assert(backing[i] == 0xcd);
        if constexpr (F::heads == 0) {
            auto dynamic =
                sp::bind_decoder<std::uint64_t>(sp::dense_input{F::width, F::storage, payload, n});
            assert(dynamic && dynamic->read(0, output) && output == truth);
        }
        effects.used = 0;
        assert(prepared->set(n - 1, truth[n - 1] ^ limit, summary, effects));
        assert(sp::get_unchecked(view, n - 1) == (truth[n - 1] ^ limit));
        for (const auto& event : effects.entries()) {
            assert(event.source == &view);
            if (event.bytes.plane == 0)
                assert(event.bytes.offset + event.bytes.size <= payload_bytes);
        }
    }
}

void check_filter_tiles() {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<std::uint8_t*>(
        mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED && !mprotect(memory + page, page, PROT_READ | PROT_WRITE));
    check_tile_widths_1(memory + 2 * page);
    check_tile_widths_9(memory + 2 * page);
    check_tile_widths_17(memory + 2 * page);
    check_tile_widths_25(memory + 2 * page);
    check_tile_widths_33(memory + 2 * page);
    check_tile_widths_41(memory + 2 * page);
    check_tile_widths_49(memory + 2 * page);
    check_tile_widths_57(memory + 2 * page);
    assert(!munmap(memory, page * 3));
    unaligned<sp::format<1>>();
    unaligned<sp::format<7>>();
    unaligned<sp::format<12>>();
    unaligned<sp::format<24>>();
    unaligned<sp::format<32>>();
    unaligned<sp::format<48>>();
    unaligned<sp::format<64>>();
    unaligned<sp::format<20, sp::geometry::striped>>();
    unaligned<sp::format<15, sp::geometry::striped>>();
    unaligned<sp::format<31, sp::geometry::striped, 16>>();
    unaligned<sp::format<64, sp::geometry::local, 8>>();
}
