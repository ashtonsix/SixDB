#pragma once
#include <ikea/tuplepack/write.h>
#include <cstring>

namespace ikea::tuplepack {
/// Complete rank-ordered code values. Slots at/above code_count are ignored.
/// Larger application records use multiple units; this does not define a struct.
template <> class write_plan<128> {
    write_plan<64> first_, second_;
    unsigned count_;
    write_plan(write_plan<64> first, write_plan<64> second, unsigned count)
        : first_(std::move(first)), second_(std::move(second)), count_(count) {}

  public:
    using input_type = packet<128>;
    [[nodiscard]] static std::expected<write_plan<128>, error> make(const layout& format);
    unsigned unit_bytes() const noexcept {
        return first_.unit_bytes();
    }
    unsigned code_count() const noexcept {
        return count_;
    }
    bool accepts(const packet<128>& input) const noexcept {
        std::array<std::uint8_t, 64> lo;
        std::memcpy(lo.data(), input.data(), 64);
        if (!first_.accepts(lo))
            return false;
        if (count_ <= 64)
            return true;
        std::array<std::uint8_t, 64> hi;
        std::memcpy(hi.data(), input.data() + 64, 64);
        return second_.accepts(hi);
    }
    /// Trusted complete initialization. Does not read old destination bytes;
    /// spare bits become zero, bytes outside the unit remain untouched.
    void initialize_unchecked(std::byte* destination, const packet<128>& input) const {
        std::array<std::uint8_t, 64> lo, hi;
        std::memcpy(lo.data(), input.data(), 64);
        if (count_ > 64)
            std::memcpy(hi.data(), input.data() + 64, 64);
        std::memset(destination, 0, unit_bytes());
        first_.set_unchecked(destination, lo);
        if (count_ > 64)
            second_.set_unchecked(destination, hi);
    }
};
/// Complete initialization only: unlike mapped writers this clears spare bits
/// and needs no prior contents. It does not expose preserving set/replace commands.
template <> class writer<128> {
    const write_plan<128>* plan_;
    const view* destination_;
    writer(const write_plan<128>& plan, const view& destination)
        : plan_(&plan), destination_(&destination) {}

  public:
    using input_type = packet<128>;
    std::size_t size() const noexcept {
        return destination_->size();
    }
    const view& destination() const noexcept {
        return *destination_;
    }
    [[nodiscard]] std::expected<std::size_t, error>
    effect_capacity_initialize(std::size_t first, std::size_t count) const noexcept {
        if (first > size() || count > size() - first)
            return std::unexpected(error::range);
        return count;
    }
    unsigned store_runs() const noexcept { return 1; }
    bool accepts(const input_type& input) const noexcept {
        return plan_->accepts(input);
    }
    template <class Visit> void visit_leaves(Visit&& visit) const {
        visit(*this);
    }
    template <class Visit> void visit_fields(Visit&& visit) const {
        for (unsigned i = 0; i < plan_->unit_bytes(); ++i)
            visit(*destination_, i, std::uint8_t(255));
    }
    [[nodiscard]] static std::expected<writer<128>, error>
    bind(const write_plan<128>& plan, const view& destination) {
        if (plan.unit_bytes() != destination.unit_bytes())
            return std::unexpected(error::description);
        return writer<128>(plan, destination);
    }
    static auto bind(const write_plan<128>&&, const view&) = delete;
    static auto bind(const write_plan<128>&, const view&&) = delete;
    /// Failure leaves the full call's data/effects unchanged. Each initialized
    /// row issues one unit-sized span, before zeroing. No replacement law runs.
    [[nodiscard]] std::expected<void, error> initialize(std::size_t first,
                                                        std::span<const packet<128>> input,
                                                        ikea::journal& effects) const {
        if (first > destination_->size() || input.size() > destination_->size() - first)
            return std::unexpected(error::range);
        if (effects.used > effects.storage.size() || input.size() > effects.remaining())
            return std::unexpected(error::effects);
        for (const auto& values : input)
            if (!plan_->accepts(values))
                return std::unexpected(error::value);
        initialize_unchecked(first, input, effects);
        return {};
    }
    /// Construction leaf for a whole compound command. Admission includes all
    /// other children first; a before-observation must not read uninitialized data.
    template <class Coverage> requires ikea::effects<Coverage, view>
    void set_unchecked(std::size_t row, const packet<128>& input, Coverage& effects) const {
        effects.before(*destination_, 0, destination_->offset() + row * destination_->stride(),
                                  plan_->unit_bytes());
        plan_->initialize_unchecked(destination_->row_unchecked(row), input);
    }
    template <class Coverage> requires ikea::effects<Coverage, view>
    void initialize_unchecked(std::size_t first, std::span<const packet<128>> input,
                              Coverage& effects) const {
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto row = first + i;
            set_unchecked(row, input[i], effects);
        }
    }
};
[[nodiscard]] inline auto bind_writer(const write_plan<128>& plan, const view& destination) {
    return writer<128>::bind(plan, destination);
}
auto bind_writer(const write_plan<128>&&, const view&) = delete;
auto bind_writer(const write_plan<128>&, const view&&) = delete;
} // namespace ikea::tuplepack
