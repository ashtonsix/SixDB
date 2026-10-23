// Prepared predicates own their code projection and borrow a named view.
// Detect expired-plan/layout reads and overruns independently of result checks.
#include <ikea/tuplepack.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace {
namespace tp = ikea::tuplepack;
using word = std::uint64_t;
using C = ikea::comparison;
template <C...> struct comparisons {};
struct Context {
    unsigned n = 0, rows = 0, width = 0, shift = 0, offset = 0, stride = 0;
    std::size_t first = 0;
    word rhs = 0, active = 0;
} context;
void need(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "predicate-check: %s; N=%u Rows=%u width=%u shift=%u offset=%u stride=%u first=%zu rhs=%llu active=%llx\n",
                     message, context.n, context.rows, context.width, context.shift, context.offset,
                     context.stride, context.first, static_cast<unsigned long long>(context.rhs),
                     static_cast<unsigned long long>(context.active));
        std::abort();
    }
}
template <class Result> auto take(Result result) {
    need(bool(result), "checked preparation");
    return std::move(*result);
}
word low(unsigned count) { return count == 64 ? ~word{0} : (word{1} << count) - 1; }
template <C Op> bool compare(word value, word rhs) {
    if constexpr (Op == C::equal) return value == rhs;
    if constexpr (Op == C::less) return value < rhs;
    if constexpr (Op == C::less_equal) return value <= rhs;
    if constexpr (Op == C::greater) return value > rhs;
    if constexpr (Op == C::greater_equal) return value >= rhs;
}

class Mapping {
    std::size_t size_;
    std::byte* base_;
  public:
    explicit Mapping(std::size_t size)
        : size_(size), base_(static_cast<std::byte*>(mmap(nullptr, size, PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))) {
        need(base_ != MAP_FAILED, "mmap");
    }
    ~Mapping() { need(munmap(base_, size_) == 0, "munmap"); }
    Mapping(const Mapping&) = delete;
    std::byte* data() const { return base_; }
    void protect(std::size_t offset, std::size_t bytes, int mode) {
        need(mprotect(base_ + offset, bytes, mode) == 0, "mprotect");
    }
};
std::size_t page_size() {
    const auto size = sysconf(_SC_PAGESIZE);
    need(size > 0, "page size");
    return std::size_t(size);
}
class Payload {
    std::size_t page_ = page_size(), writable_;
    Mapping map_;
    std::span<std::byte> bytes_;
  public:
    explicit Payload(std::size_t bytes)
        : writable_((bytes + page_ - 1) / page_ * page_), map_(writable_ + 2 * page_),
          bytes_(map_.data() + page_ + writable_ - bytes, bytes) {
        map_.protect(page_, writable_, PROT_READ | PROT_WRITE);
        for (std::size_t i = 0; i < bytes; ++i)
            bytes_[i] = std::byte((i * 71 + (i / 7) * 13 + 0xa5) & 255);
    }
    std::span<std::byte> bytes() const { return bytes_; }
    void hide() { map_.protect(page_, writable_, PROT_NONE); }
};
template <class Plan> class PlanPage {
    std::size_t bytes_ = (sizeof(Plan) + page_size() - 1) / page_size() * page_size();
    Mapping map_{bytes_};
    Plan* plan_ = nullptr;
  public:
    ~PlanPage() { if (plan_) std::destroy_at(plan_); }
    const Plan& construct(Plan value) {
        map_.protect(0, bytes_, PROT_READ | PROT_WRITE);
        need(reinterpret_cast<std::uintptr_t>(map_.data()) % alignof(Plan) == 0, "plan alignment");
        plan_ = std::construct_at(reinterpret_cast<Plan*>(map_.data()), std::move(value));
        return *plan_;
    }
    void expire() {
        std::destroy_at(plan_); plan_ = nullptr;
        map_.protect(0, bytes_, PROT_NONE);
    }
};

// Read physical bits directly. No plan, packet ordering, Ikea comparison helper
// or encoder participates in the expected result.
word decode(std::span<const std::byte> data, std::size_t row, unsigned stride,
            unsigned offset, unsigned shift, unsigned width) {
    const auto byte = std::to_integer<unsigned>(data[row * stride + offset]);
    word value = 0;
    for (unsigned bit = 0; bit < width; ++bit)
        value |= word((byte >> (shift + bit)) & 1) << bit;
    return value;
}
template <unsigned Rows, C Op, class Operation>
void readable(const Operation& operation, std::span<const std::byte> data, unsigned count,
              unsigned stride, unsigned offset, unsigned shift, unsigned width) {
    const unsigned tail = Rows == 1 ? 1 : Rows / 2 + 1;
    for (unsigned first : {0u, 3u, count - tail, count - 1}) {
        const unsigned available = std::min<unsigned>(Rows, count - first);
        const word full = low(available);
        const std::array masks{word{0}, full, word{1}, word{1} << (available - 1),
                              low((available + 1) / 2), full & ~low(available / 2),
                              full & (low(2) | (word{3} << (available > 3 ? available - 2 : 0)))};
        const word limit = low(width);
        const auto present = decode(data, first, stride, offset, shift, width);
        const std::array queries{word{0}, word{1}, limit / 2, limit, limit + 1, present,
                                 word{127}, word{128}, word{0x80000000}, word{0xffffffff},
                                 word{1} << 32, word{1} << 63, ~word{0}};
        for (word active : masks) for (word rhs : queries) {
            context.first = first; context.rhs = rhs; context.active = active;
            word wanted = 0;
            for (unsigned row = 0; row < available; ++row)
                if ((active & (word{1} << row)) &&
                    compare<Op>(decode(data, first + row, stride, offset, shift, width), rhs))
                    wanted |= word{1} << row;
            const auto result = operation.template filter<Op>(first, rhs, active);
            need(bool(result) && *result == wanted, "byte oracle / original-row mask");
        }
    }
}
template <unsigned Rows, C Op, class Operation>
void inaccessible(const Operation& operation, unsigned count, unsigned width) {
    for (std::size_t first : {std::size_t{0}, std::size_t(count)}) {
        context.first = first; context.rhs = 0; context.active = 0;
        const auto empty = operation.template filter<Op>(first, 0, 0);
        need(bool(empty) && *empty == 0, "empty protected payload");
    }
    const auto active = low(Rows);
    for (word rhs : {low(width) + 1, word{0x80000000}, word{0xffffffff},
                     word{1} << 32, word{1} << 63, ~word{0}}) {
        context.first = 3; context.rhs = rhs; context.active = active;
        const auto result = operation.template filter<Op>(3, rhs, active);
        need(bool(result) && *result == (compare<Op>(0, rhs) ? active : 0),
             "out-of-domain protected payload / full-width unsigned rhs");
    }
    auto rejected = [&](std::size_t first, word mask) {
        context.first = first; context.rhs = ~word{0}; context.active = mask;
        const auto result = operation.template filter<Op>(first, ~word{0}, mask);
        need(!result && result.error() == tp::error::range, "range admission precedes shortcuts");
    };
    rejected(count + 1, 0); rejected(count, 1);
    rejected(std::numeric_limits<std::size_t>::max(), 0);
    if constexpr (Rows > 1) rejected(count - 1, 2);
    if constexpr (Rows < 64) rejected(0, word{1} << Rows);
}
template <C Op, class Reader> void map_refuses(const Reader& reader, unsigned count) {
    const auto result = reader.template filter<Op>(count + 1, ~word{0}, 0);
    need(!result && result.error() == tp::error::map, "map failure precedes command shortcuts");
}
template <unsigned N, unsigned Rows>
void check_geometry(unsigned width, unsigned shift, unsigned unit, unsigned stride) {
    context = {N, Rows, width, shift, unit - 1, stride};
    const unsigned count = Rows + 3, offset = unit - 1;
    Payload payload((count - 1) * stride + unit);
    const auto law = take(tp::layout::make(unit, std::array<tp::code, 1>{
        {{std::uint8_t(offset), std::uint8_t(shift), std::uint8_t(width)}}}));
    const auto source = take(tp::const_view::attach(law, payload.bytes(), count, stride));
    const auto direct = take(tp::bind_predicate<Rows>(law, 0, source));
    using Plan = tp::read_plan<N, Rows>;
    PlanPage<Plan> storage;
    const auto predicates = [&] {
        const auto& plan = storage.construct(take(Plan::make(law, std::array<std::uint8_t, 1>{0})));
        const auto reader = take(tp::bind_reader(plan, source));
        [&]<C... Ops>(comparisons<Ops...>) {
            (readable<Rows, Ops>(reader, payload.bytes(), count, stride, offset, shift, width), ...);
        }(comparisons<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
        return std::pair{take(tp::bind_predicate(reader)), take(tp::bind_predicate(plan, source))};
    }(); // Reader wrapper expires before the actual mmap-placed plan.
    const auto temporary_plan = take(tp::bind_predicate(
        take(Plan::make(law, std::array<std::uint8_t, 1>{0})), source));
    storage.expire();
    need(predicates.first.size() == count && predicates.second.size() == count, "size after plan expiry");
    [&]<C... Ops>(comparisons<Ops...>) {
        (readable<Rows, Ops>(predicates.first, payload.bytes(), count, stride, offset, shift, width), ...);
        (readable<Rows, Ops>(predicates.second, payload.bytes(), count, stride, offset, shift, width), ...);
        (readable<Rows, Ops>(temporary_plan, payload.bytes(), count, stride, offset, shift, width), ...);
        (readable<Rows, Ops>(direct, payload.bytes(), count, stride, offset, shift, width), ...);
    }(comparisons<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    payload.hide();
    [&]<C... Ops>(comparisons<Ops...>) {
        (inaccessible<Rows, Ops>(predicates.first, count, width), ...);
        (inaccessible<Rows, Ops>(predicates.second, count, width), ...);
        (inaccessible<Rows, Ops>(temporary_plan, count, width), ...);
        (inaccessible<Rows, Ops>(direct, count, width), ...);
    }(comparisons<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    for (unsigned kind = 0; kind < (N / Rows > 1 ? 3u : 2u); ++kind) {
        const std::array<std::uint8_t, 2> map = kind == 0 ? std::array<std::uint8_t, 2>{tp::hole, 0}
                                                        : std::array<std::uint8_t, 2>{0, 0};
        const auto span = std::span(map).first(kind == 0 ? 1 : kind == 1 ? 0 : 2);
        const auto bad_plan = take(Plan::make(law, span));
        const auto reader = take(tp::bind_reader(bad_plan, source));
        const auto bound = tp::bind_predicate(reader);
        need(!bound && bound.error() == tp::error::map, "invalid map binding touches no payload");
        const auto direct = tp::bind_predicate(bad_plan, source);
        need(!direct && direct.error() == tp::error::map, "direct invalid map binding touches no payload");
        [&]<C... Ops>(comparisons<Ops...>) {
            (map_refuses<Ops>(reader, count), ...);
        }(comparisons<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    }
    const auto wrong_law = take(tp::layout::make(unit == 1 ? 2 : 1,
                                                std::array<tp::code, 1>{{{0, 0, 1}}}));
    const auto wrong_view = take(tp::const_view::attach(wrong_law,
        {payload.bytes().data(), 0}, 0, wrong_law.bytes()));
    const auto compatible_plan = take(Plan::make(law, std::array<std::uint8_t, 1>{0}));
    const auto mismatch = tp::bind_predicate(compatible_plan, wrong_view);
    need(!mismatch && mismatch.error() == tp::error::description, "placement mismatch before binding");
}
template <unsigned Rows> void layout_binding() {
    constexpr unsigned count = Rows + 3, unit = 64, stride = 67;
    context = {0, Rows, 1, 7, 63, stride};
    std::array<tp::code, 128> codes;
    for (unsigned i = 0; i < codes.size(); ++i)
        codes[i] = {std::uint8_t(i / 2), std::uint8_t(i % 2 ? 7 : 0), 1};
    PlanPage<tp::layout> storage;
    const auto& law = storage.construct(take(tp::layout::make(unit, codes)));
    Payload payload((count - 1) * stride + unit);
    const auto mutable_source = take(tp::view::attach(law, payload.bytes(), count, stride));
    const auto source = mutable_source.as_const();
    constexpr std::array<std::size_t, 6> indices{0, 1, 63, 64, 126, 127};
    const auto predicates = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array{take(tp::bind_predicate<Rows>(law, indices[I], source))...};
    }(std::make_index_sequence<indices.size()>{});
    const auto mutable_predicate = take(tp::bind_predicate<Rows>(law, 127, mutable_source));
    const auto static_predicate = take(tp::predicate<const std::byte, Rows>::bind(law, 127, source));
    const auto temporary_layout = take(tp::bind_predicate<Rows>(
        take(tp::layout::make(unit, codes)), 127, source));

    Payload hidden((count - 1) * stride + unit);
    const auto protected_source = take(tp::const_view::attach(law, hidden.bytes(), count, stride));
    hidden.hide();
    const auto protected_predicate = take(tp::bind_predicate<Rows>(law, 127, protected_source));
    for (const auto index : {std::size_t{128}, std::size_t{255}, std::size_t{256},
                             std::numeric_limits<std::size_t>::max() / 2 + 1,
                             std::numeric_limits<std::size_t>::max()}) {
        const auto rejected = tp::bind_predicate<Rows>(law, index, protected_source);
        need(!rejected && rejected.error() == tp::error::map, "layout code index before payload access");
    }
    const auto other = take(tp::layout::make(1, std::array<tp::code, 1>{{{0, 0, 1}}}));
    const auto wrong_view = take(tp::const_view::attach(other, {hidden.bytes().data(), 0}, 0, 1));
    for (const auto index : {std::size_t{0}, std::numeric_limits<std::size_t>::max()}) {
        const auto rejected = tp::bind_predicate<Rows>(law, index, wrong_view);
        need(!rejected && rejected.error() == tp::error::description,
             "layout extent mismatch precedes code index / payload access");
    }
    storage.expire(); // Destroy the actual bound layout, then protect its page.
    for (unsigned i = 0; i < indices.size(); ++i) {
        const auto code = codes[indices[i]];
        context.offset = code.offset; context.shift = code.shift;
        [&]<C... Ops>(comparisons<Ops...>) {
            (readable<Rows, Ops>(predicates[i], payload.bytes(), count, stride,
                                  code.offset, code.shift, code.width), ...);
        }(comparisons<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    }
    context.offset = 63; context.shift = 7;
    [&]<C... Ops>(comparisons<Ops...>) {
        (readable<Rows, Ops>(mutable_predicate, payload.bytes(), count, stride, 63, 7, 1), ...);
        (readable<Rows, Ops>(static_predicate, payload.bytes(), count, stride, 63, 7, 1), ...);
        (readable<Rows, Ops>(temporary_layout, payload.bytes(), count, stride, 63, 7, 1), ...);
        (inaccessible<Rows, Ops>(protected_predicate, count, 1), ...);
    }(comparisons<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
}
template <unsigned N, unsigned Rows> void shape() {
    using Plan = tp::read_plan<N, Rows>;
    static_assert(!requires(const Plan& plan, tp::const_view view) {
        tp::bind_predicate(plan, std::move(view));
    });
    static_assert(!requires(Plan plan, tp::const_view view) {
        tp::bind_predicate(std::move(plan), std::move(view));
    });
    static_assert(!requires(const tp::layout& law, tp::const_view view) {
        tp::bind_predicate<Rows>(law, 0, std::move(view));
    });
    static_assert(!requires(tp::layout law, tp::view view) {
        tp::bind_predicate<Rows>(std::move(law), 0, std::move(view));
    });
    static_assert(!requires(const tp::layout& law, tp::view view) {
        tp::predicate<std::byte, Rows>::bind(law, 0, std::move(view));
    });
    static_assert(requires(tp::layout law, const tp::const_view& view) {
        tp::bind_predicate<Rows>(std::move(law), 0, view);
    });
    using Direct = typename decltype(tp::bind_predicate<Rows>(
        std::declval<const tp::layout&>(), 0, std::declval<const tp::const_view&>()))::value_type;
    using Planned = typename decltype(tp::bind_predicate(
        std::declval<const Plan&>(), std::declval<const tp::const_view&>()))::value_type;
    static_assert(std::is_same_v<Direct, Planned>, "direct and planned predicates have the same type");
    if constexpr (Rows <= 8) {
        using Small = typename decltype(tp::bind_predicate(
            std::declval<const tp::read_plan<8, Rows>&>(), std::declval<const tp::const_view&>()))::value_type;
        using Large = typename decltype(tp::bind_predicate(
            std::declval<const tp::read_plan<64, Rows>&>(), std::declval<const tp::const_view&>()))::value_type;
        static_assert(std::is_same_v<Small, Large>, "predicate shape is independent of preparation N");
    }
    for (unsigned width = 1; width <= 8; ++width)
        for (unsigned shift = 0; shift + width <= 8; ++shift)
            for (unsigned placement = 0; placement < 3; ++placement)
                check_geometry<N, Rows>(width, shift, placement == 0 ? 1 : 64,
                                       placement == 0 ? 1 : placement == 1 ? 64 : 67);
}
} // namespace
int main() {
    shape<8, 1>(); shape<8, 2>(); shape<8, 4>(); shape<8, 8>();
    shape<64, 1>(); shape<64, 2>(); shape<64, 4>(); shape<64, 8>();
    shape<64, 16>(); shape<64, 32>(); shape<64, 64>();
    layout_binding<1>(); layout_binding<2>(); layout_binding<4>(); layout_binding<8>();
    layout_binding<16>(); layout_binding<32>(); layout_binding<64>();
    std::puts("predicate-check: 11 shapes, 36 width/shift laws, 3 placements, 5 unsigned comparisons; expired plans/layouts, direct code ranks through 127, guarded exact tails, masks and admission passed");
}
