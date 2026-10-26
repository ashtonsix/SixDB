#include <ikea/bitset256.h>
#include <ikea/seriespack/selection.h>
#include <ikea/tuplepack/selection.h>
#include <ikea/intervals/types.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace in = ikea::intervals;
using ikea::bitset256;
template<class Selection> concept borrows_temporary_block = requires {
    Selection::bits(0, bitset256{});
};
static_assert(!borrows_temporary_block<sp::selection> && !borrows_temporary_block<tp::selection> &&
              !borrows_temporary_block<in::selection>);

// Expected values come from position flags, never the mask/word implementation.
std::uint64_t expected_mask(const std::array<bool, 256>& flags, unsigned first, unsigned count) {
    std::uint64_t result = 0;
    for (unsigned r = 0; r < count; ++r)
        if (flags[first + r]) result |= std::uint64_t{1} << r;
    return result;
}
template<class Selection>
void windows(const Selection& selected, const std::array<bool, 256>& flags, std::size_t origin) {
    assert(selected.covers(origin, 256));
    assert(!selected.covers(origin, 257));
    if (origin) assert(!selected.covers(origin - 1, 1));
    assert(!selected.covers(origin + 256, 1));
    for (unsigned first = 0; first <= 256; ++first) {
        if (first < 256) assert(selected.contains(origin + first) == flags[first]);
        for (unsigned count = 0; count <= 64 && count <= 256 - first; ++count)
            assert(selected.mask(origin + first, count) == expected_mask(flags, first, count));
    }
}
template<class Selection> void terminal() {
    assert(Selection{}.is_none());
    const auto all = Selection::all(), none = Selection::none();
    assert(all.is_all() && !all.is_none() && none.is_none() && !none.is_all());
    assert(all.covers(0, 256) && none.covers(0, 256));
    const auto max = std::numeric_limits<std::size_t>::max();
    assert(all.covers(max, 1) && none.covers(max, 1));
    assert(!all.covers(max, 2) && !none.covers(max, 2));
    assert(all.mask(max, 0) == 0 && none.mask(max, 0) == 0);
    assert(all.contains(max) && !none.contains(max));
    assert(all.mask(13, 64) == ~std::uint64_t{0} && none.mask(13, 64) == 0);
}
void blocks() {
    const bitset256 zero{};
    assert(zero.empty() && !zero.full() && zero.count() == 0 && zero.rank(256) == 0);
    for (unsigned pattern = 0; pattern < 260; ++pattern) {
        std::array<bool,256> flags{};
        bitset256 value;
        std::array<std::byte,32> bytes{};
        std::array<std::uint16_t,16> small{};
        std::array<std::uint64_t,4> wide{};
        for (unsigned i = 0; i < 256; ++i) {
            flags[i] = pattern < 256 ? i == pattern
                       : pattern == 256 ? true
                       : pattern == 257 ? false
                       : pattern == 258 ? i % 3 == 0 : (i * 73 + 19) % 11 < 5;
            if (flags[i]) {
                value.set(i);
                bytes[i / 8] |= std::byte(1u << (i % 8));
                small[i / 16] |= std::uint16_t(1u << (i % 16));
                wide[i / 64] |= std::uint64_t{1} << (i % 64);
            }
        }
        unsigned population = 0;
        for (unsigned i = 0; i <= 256; ++i) {
            assert(value.rank(i) == population);
            if (i < 256 && flags[i]) ++population;
        }
        assert(value.count() == population && value.empty() == !population && value.full() == (population == 256));
        unsigned next = 0, visited = 0;
        value.for_each([&](unsigned position) {
            while (next < 256 && !flags[next]) ++next;
            assert(position == next);
            ++next;
            ++visited;
        });
        assert(visited == population);
        for (unsigned n = 0; n <= 256; ++n) {
            const auto prefix = bitset256::first_n(n);
            const auto intersection = value & prefix;
            for (unsigned i = 0; i < 256; ++i) {
                assert(prefix.contains(i) == (i < n));
                assert(intersection.contains(i) == (flags[i] && i < n));
            }
        }
        std::array<std::byte,32> stored;
        value.store(stored);
        assert(stored == bytes && bitset256::load(bytes) == value);
        for (unsigned first = 0; first <= 256; ++first)
            for (unsigned count = 0; count <= 64 && count <= 256 - first; ++count)
                assert(value.mask(first,count) == expected_mask(flags,first,count));
        windows(sp::selection::bits(32, value), flags, 32);
        windows(tp::selection::bits(7, value), flags, 7);
        windows(in::selection::bits(3, value), flags, 3);
        windows(sp::selection::bits(32, small), flags, 32);
        windows(tp::selection::bits(7, wide), flags, 7);
        windows(in::selection::bits(3, bytes), flags, 3);
        for (auto& flag : flags) flag = !flag;
        windows(in::selection::complement(3, bytes), flags, 3);
        windows(in::selection::complement(3, value), flags, 3);
        const auto eligible = in::selection::complement(3, value);
        for (unsigned first = 0; first <= 262; ++first) {
            unsigned next = 262;
            for (unsigned i = first; i < 262; ++i)
                if (i >= 3 && i < 259 && flags[i-3]) { next = i; break; }
            assert(eligible.next(first, 262) == next);
            unsigned previous = 262;
            for (unsigned i = first; i; --i)
                if (i - 1 >= 3 && i - 1 < 259 && flags[i-1-3]) { previous = i-1; break; }
            assert(eligible.previous(first, 262) == previous);
        }
        for (unsigned i = 0; i < 256; ++i) value.set(i, false);
        assert(value.empty());
    }
}
void block_sequences() {
    std::array<bitset256,3> blocks;
    std::array<bool,768> flags{};
    for (unsigned r=0;r<flags.size();++r) {
        flags[r]=(r*17+3)%23<9;
        if(flags[r]) blocks[r/256].set(r%256);
    }
    const auto selected=sp::selection::bits(32,std::span<const bitset256>(blocks));
    assert(selected.covers(32,768) && !selected.covers(31,1) && !selected.covers(32,769));
    for (unsigned first=0;first<=768;++first) {
        if(first<768) assert(selected.contains(32+first)==flags[first]);
        for(unsigned n=0;n<=64 && first+n<=768;++n) {
            std::uint64_t expected=0;
            for(unsigned r=0;r<n;++r) if(flags[first+r]) expected|=std::uint64_t{1}<<r;
            assert(selected.mask(32+first,n)==expected);
        }
    }
    const auto regions=selected.region_words();
    for(unsigned r=0;r<768;r+=16) assert(regions(32+r)==selected.mask(32+r,16));
    assert(!sp::selection::bits(1,std::span<const bitset256>(blocks)).covers(1,1));
    assert(!sp::selection::bits(0,std::span<const bitset256>{}).covers(0,1));
}
void exact_tails() {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<std::byte*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0);
    for (unsigned n = 1; n <= 256; ++n) {
        const auto bytes = (n + 7) / 8;
        auto* p = memory + page - bytes;
        for (unsigned b = 0; b < bytes; ++b) p[b] = std::byte{0};
        p[(n-1)/8] = std::byte(1u << ((n-1)%8));
        const auto selected = in::selection::bits(5, {p,bytes});
        const auto excluded = in::selection::complement(5, {p,bytes});
        assert(selected.next(5,5+n) == 5+n-1 && selected.previous(5+n,5+n) == 5+n-1);
        for (unsigned first = 0; first <= n; ++first)
            for (unsigned count = 0; count <= 64 && count <= n-first; ++count) {
                const auto want = count && first+count == n ? std::uint64_t{1} << (count-1) : 0;
                const auto limit = !count ? 0 : ~std::uint64_t{0} >> (64-count);
                assert(selected.mask(5+first,count) == want);
                assert(excluded.mask(5+first,count) == (want ^ limit));
            }
    }
    assert(munmap(memory,page*2) == 0);
}
void coordinate_limits() {
    const auto max = std::numeric_limits<std::size_t>::max();
    const std::array<std::uint16_t,1> small{0x8000};
    const std::array<std::uint64_t,1> wide{std::uint64_t{1} << 63};
    const std::array<std::byte,1> bytes{std::byte{0x80}};
    const auto a = sp::selection::bits(max - 15, small);
    const auto b = tp::selection::bits(max - 63, wide);
    const auto c = in::selection::bits(max - 7, bytes);
    assert(a.covers(max,1) && b.covers(max,1) && c.covers(max,1));
    assert(!a.covers(max,2) && !b.covers(max,2) && !c.covers(max,2));
    assert(a.contains(max) && b.contains(max) && c.contains(max));
    assert(a.mask(max,1) == 1 && b.mask(max,1) == 1 && c.mask(max,1) == 1);
    assert(!sp::selection::bits(0,std::span<const std::uint16_t>{}).covers(0,1));
    assert(!tp::selection::bits(0,std::span<const std::uint64_t>{}).covers(0,1));
    const auto empty = in::selection::complement(0,std::span<const std::byte>{});
    assert(!empty.covers(0,1) && empty.next(0,256) == 256 && empty.previous(256,256) == 256);
}
}
int main() {
    terminal<sp::selection>(); terminal<tp::selection>(); terminal<in::selection>();
    const std::array<std::uint16_t,1> word{1};
    assert(!sp::selection::bits(1,word).covers(1,1));
    blocks(); block_sequences(); exact_tails(); coordinate_limits();
    std::puts("selection: all256 positions, rank endpoints, every bounded window, origins, complements and exact protected tails pass");
}
