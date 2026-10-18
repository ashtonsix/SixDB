// K-way remains research; the independent ordered-history fixture is shared
// with Ikea's maintained accumulation checks. Physical codec checks live there.
#include "candidate.h"
#include "../../../ikea/test/hashtable/accumulation-history.h"
namespace ms = merge_study;
namespace ht = ikea::hashtable;
using ht::word;
using hashtable_test::identity;
using hashtable_test::payload;
template <unsigned W, unsigned sb, unsigned db, unsigned V, unsigned SE = W - sb,
          unsigned DE = W - db>
void history() {
    hashtable_test::ordered_history<W, sb, db, V, SE, DE>(
        [](auto& dst, auto sources, auto& reduce, std::size_t spills) {
            using D = std::remove_reference_t<decltype(dst)>;
            using S = std::remove_cvref_t<decltype(*sources[0])>;
            ms::kway_workspace work;
            work.reserve(ms::group_count<D, S>, spills);
            return ms::accumulate_kway(dst, sources, work, reduce) == ms::merge_status::complete;
        });
}
void refusals() {
    using T = ht::table<24, 2, 8>;
    ht::permutation<24, 22> mix{713};
    T src(713), dst(713);
    for (unsigned q = 0; q < T::N + 80; ++q)
        src.insert(mix.recover(0, q), payload<8>(q, 1));
    const auto before = identity(src);
    const std::array<const T*, 1> inputs{&src};
    ms::kway_workspace work;
    auto reduce = [](const auto& a, const auto&) { return a; };
    ht::require(ms::accumulate_kway(dst, std::span<const T* const>(inputs), work, reduce) ==
                    ms::merge_status::needs_workspace,
                "workspace refusal");
    ht::require(ms::accumulate(dst, src, reduce) == ms::merge_status::needs_overflow,
                "fixed overflow refusal"); // discard dst
    T alias(713);
    ht::require(ms::accumulate(alias, alias, reduce) == ms::merge_status::alias, "alias rejected");
    T foreign(714), fresh(713);
    ht::require(ms::accumulate(fresh, foreign, reduce) == ms::merge_status::incompatible,
                "seed rejected");
    T target(713);
    target.insert(mix.recover(0, 0), payload<8>(0, 0));
    ms::reserve_overflow(target, src.size() + 1);
    bool threw = false;
    try {
        (void)ms::accumulate(target, src, [](const auto&, const auto&) -> ht::bytes<8> {
            throw std::runtime_error("reducer");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ht::require(threw && identity(src) == before,
                "throw leaves source unchanged"); // discard target
    T ktarget(713);
    ktarget.insert(mix.recover(0, 0), payload<8>(0, 0));
    ms::reserve_overflow(ktarget, src.size() + 1);
    work.reserve(T::bucket_count, ms::access::get(src).spill);
    threw = false;
    try {
        (void)ms::accumulate_kway(ktarget, std::span<const T* const>(inputs), work,
                                  [](const auto&, const auto&) -> ht::bytes<8> {
                                      throw std::runtime_error("ordered reducer");
                                  });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ht::require(threw && identity(src) == before,
                "K-way throw preserves sources"); // discard ktarget
    T empty(713);
    ms::kway_workspace unused;
    ht::require(ms::accumulate_kway(empty, std::span<const T* const>{}, unused, reduce) ==
                    ms::merge_status::complete,
                "empty ordered source list");
    // Cleanup after sticky overflow saturation and tombstones happens outside merge.
    T saturated(713);
    for (unsigned q = 0; q < 340; ++q)
        saturated.insert(mix.recover(0, q), payload<8>(q, 0));
    for (unsigned q = 0; q < 200; ++q)
        saturated.erase(mix.recover(0, q));
    ms::reserve_overflow(saturated, 400);
    saturated.validate();
}
int main() {
    try {
        history<1, 0, 1, 0>();
        history<8, 1, 0, 3>();
        history<10, 0, 1, 9>();
        history<15, 1, 2, 8, 14, 14>();
        history<24, 3, 1, 8>();
        history<32, 1, 2, 64>();
        history<34, 1, 4, 8, 40, 30>();
        history<24, 10, 13, 8, 14, 11>();
        history<24, 10, 13, 64, 14, 11>();
        history<24, 10, 11, 8, 14, 14>();
        history<40, 10, 13, 8, 30, 28>();
        history<40, 10, 13, 3, 30, 29>();
        history<40, 10, 13, 9, 30, 29>();
        history<34, 10, 10, 8, 24, 24>();
        history<40, 10, 13, 8, 30, 30>();
        history<40, 10, 13, 64, 30, 30>();
        history<45, 15, 18, 8, 30, 29>();
        history<64, 0, 1, 8, 64, 64>();
        history<24, 2, 2, 0>();
        history<24, 2, 2, 9>();
        history<57, 10, 12, 8, 64, 46>();
        history<57, 10, 12, 64, 64, 46>();
        history<56, 10, 10, 8, 46, 46>();
        history<48, 8, 9, 3, 40, 40>();
        history<41, 10, 10, 8, 31, 31>();
        history<41, 10, 11, 8, 31, 30>();
        refusals();
        std::cout << "PASS ordered mutable and bucket-wise K-way histories\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
