#pragma once
#include <ikea2/seriespack/author/expression.h>
#include <vector>

namespace ikea2::seriespack::composition {
/// Provisional in-process inspection of an authored operation. Engine owns
/// persistent schema/plan identities and serialization; this is not that schema.
enum class instruction { rows, active, load, join, unsigned_less, modulo_sum };
struct record_node {
    instruction operation;
    unsigned bits = 0;
    std::size_t left = 0, right = 0, selection = 0;
    const void* source = nullptr;
    field_kind field = field_kind::body;
    std::uint64_t constant = 0;
};
template <unsigned K> struct recorded_value {
    std::size_t id;
};
struct recorded_rows {
    std::size_t id;
};
struct recorded_mask {
    std::size_t id;
};

/// Owns nodes, borrows source identities. Recording never reads source bytes.
/// A load's selection denotes logical activity, not permission to read fewer
/// bytes. A chosen executor must admit each actual dependency independently.
class recorder {
    std::vector<record_node> nodes_;
    std::size_t append(record_node n) {
        nodes_.push_back(n);
        return nodes_.size() - 1;
    }

  public:
    const auto& nodes() const {
        return nodes_;
    }
    recorded_rows rows() {
        return {append({instruction::rows})};
    }
    recorded_mask active() {
        return {append({instruction::active})};
    }
    template <class S, field_kind F>
    auto read(const leaf<S, F>& field, recorded_rows rows, recorded_mask active) {
        record_node n{instruction::load, leaf<S, F>::bit_width, rows.id, 0, active.id};
        n.source = &field.source;
        n.field = F;
        return recorded_value<leaf<S, F>::bit_width>{append(n)};
    }
    template <unsigned Shift, unsigned A, unsigned B>
    auto join(recorded_value<A> high, recorded_value<B> low) {
        static_assert(B <= Shift && A + Shift <= 64);
        record_node n{instruction::join, A + Shift, high.id, low.id};
        n.constant = Shift;
        return recorded_value<A + Shift>{append(n)};
    }
    template <unsigned K>
    recorded_mask unsigned_less(recorded_value<K> x, std::uint64_t cutoff, recorded_mask active) {
        record_node n{instruction::unsigned_less, K, x.id, 0, active.id};
        n.constant = cutoff;
        return {append(n)};
    }
    template <unsigned K> recorded_value<64> sum(recorded_value<K> x, recorded_mask selected) {
        return {append({instruction::modulo_sum, 64, x.id, 0, selected.id})};
    }
};
} // namespace ikea2::seriespack::composition
