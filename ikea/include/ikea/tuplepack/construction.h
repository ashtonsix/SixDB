#pragma once
#include <ikea/tuplepack/write.h>
#include <cstring>

namespace ikea::tuplepack {
/// Complete rank-ordered code values. Slots at/above code_count are ignored.
/// Larger application records use multiple units; this does not define a struct.
using construction_input = std::array<byte, 128>;
class constructor {
    writer<64> first_, second_;
    unsigned count_;
    constructor(writer<64> first, writer<64> second, unsigned count)
        : first_(std::move(first)), second_(std::move(second)), count_(count) {}

  public:
    using input_type = construction_input;
    [[nodiscard]] static std::expected<constructor, error> make(const layout& format);
    unsigned unit_bytes() const noexcept {
        return first_.unit_bytes();
    }
    unsigned code_count() const noexcept {
        return count_;
    }
    bool accepts(const construction_input& input) const noexcept {
        std::array<byte, 64> lo, hi;
        std::memcpy(lo.data(), input.data(), 64);
        std::memcpy(hi.data(), input.data() + 64, 64);
        return first_.accepts(lo) && second_.accepts(hi);
    }
    /// Trusted complete initialization. Does not read old destination bytes;
    /// spare bits become zero, bytes outside the unit remain untouched.
    void initialize_unchecked(byte* destination, const construction_input& input) const {
        std::array<byte, 64> lo, hi;
        std::memcpy(lo.data(), input.data(), 64);
        std::memcpy(hi.data(), input.data() + 64, 64);
        std::memset(destination, 0, unit_bytes());
        first_.set_unchecked(destination, lo);
        second_.set_unchecked(destination, hi);
    }
};
class construction_operation {
    const constructor* plan_;
    const view* destination_;
    construction_operation(const constructor& plan, const view& destination)
        : plan_(&plan), destination_(&destination) {}

  public:
    using input_type = construction_input;
    std::size_t size() const noexcept {
        return destination_->size();
    }
    const view& destination() const noexcept {
        return *destination_;
    }
    unsigned effect_capacity() const noexcept {
        return 1;
    }
    bool accepts(const input_type& input) const noexcept {
        return plan_->accepts(input);
    }
    template <class Visit> void visit_leaves(Visit&& visit) const {
        visit(*this);
    }
    template <class Visit> void visit_fields(Visit&& visit) const {
        for (unsigned i = 0; i < plan_->unit_bytes(); ++i)
            visit(*destination_, i, byte(255));
    }
    [[nodiscard]] static std::expected<construction_operation, error>
    bind(const constructor& plan, const view& destination) {
        if (plan.unit_bytes() != destination.unit_bytes())
            return std::unexpected(error::description);
        return construction_operation(plan, destination);
    }
    static auto bind(const constructor&&, const view&) = delete;
    static auto bind(const constructor&, const view&&) = delete;
    /// Failure leaves the full call's data/effects unchanged. Each initialized
    /// row issues one unit-sized span, before zeroing. No replacement law runs.
    [[nodiscard]] std::expected<void, error> initialize(std::size_t first,
                                                        std::span<const construction_input> input,
                                                        source_write_journal& effects) const {
        if (first > destination_->size() || input.size() > destination_->size() - first)
            return std::unexpected(error::range);
        if (effects.used > effects.storage.size() || input.size() > effects.remaining())
            return std::unexpected(error::capacity);
        for (const auto& values : input)
            if (!plan_->accepts(values))
                return std::unexpected(error::value);
        initialize_unchecked(first, input, effects);
        return {};
    }
    /// Construction leaf for a whole compound command. Admission includes all
    /// other children first; a before-observation must not read uninitialized data.
    template <class Coverage>
    void set_unchecked(std::size_t row, const construction_input& input, Coverage& effects) const {
        effects.before(*destination_,
                       byte_write{0, destination_->offset() + row * destination_->stride(),
                                  plan_->unit_bytes()});
        plan_->initialize_unchecked(destination_->row_unchecked(row), input);
    }
    template <class Coverage>
    void initialize_unchecked(std::size_t first, std::span<const construction_input> input,
                              Coverage& effects) const {
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto row = first + i;
            set_unchecked(row, input[i], effects);
        }
    }
};
inline auto bind_constructor(const constructor& plan, const view& destination) {
    return construction_operation::bind(plan, destination);
}
auto bind_constructor(const constructor&&, const view&) = delete;
auto bind_constructor(const constructor&, const view&&) = delete;
} // namespace ikea::tuplepack
