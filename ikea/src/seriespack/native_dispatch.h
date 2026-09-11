#pragma once

#include <ikea/seriespack/operations.h>

namespace ikea::seriespack::detail {

// Reports compiled implementations, not runtime CPU probing. The process is
// running on the ISA selected by the build. Automatic/scalar selection belongs
// to the outer dispatcher; these entries accept an available native target.
[[nodiscard]] bool native_target_available(execution_target target) noexcept;
[[nodiscard]] bound_reader::point_function
select_point_function(description layout, point_reader strategy);
[[nodiscard]] std::expected<bound_reader, error>
bind_native_reader(const_view source, execution_target target, point_reader strategy);
[[nodiscard]] std::expected<bound_encoder, error>
bind_native_encoder(mutable_view destination, execution_target target);
void encode_effects(const mutable_view& destination, effect_output* effects);

// Source fit, extents, disjointness and target availability are established by
// the outer operation. No validation, allocation or effect callbacks occur here.
void native_encode(mutable_view& destination, input_values input, execution_target target);

} // namespace ikea::seriespack::detail
