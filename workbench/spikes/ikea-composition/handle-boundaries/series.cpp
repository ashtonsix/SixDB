#include "probe.h"

namespace hb {
#define HB_PASS(name, type) HB_NOINLINE type pass_##name(type value) { return value; }
HB_SERIES_RAW_TYPES(HB_PASS)
#undef HB_PASS

HB_NOINLINE dense_reader make_dense(dense_source source) { return sp::bind_reader(source); }
HB_NOINLINE erased_series_reader make_erased_dense(dense_source source) {
    return sp::bind_reader<word>(source);
}
HB_NOINLINE std::expected<erased_series_reader, sp::error>
make_runtime_dense(const sp::dense_input& source) {
    return sp::bind_reader<word>(source);
}
HB_NOINLINE std::expected<series_writer, sp::error> make_series_writer(const series_view& source) {
    return sp::bind_writer(source);
}
HB_NOINLINE word consume_dense(dense_reader operation, std::size_t row) {
    return operation.get_unchecked(row);
}
HB_NOINLINE word consume_erased_dense(erased_series_reader operation, std::size_t row) {
    return operation.get_unchecked(row);
}
word dense_roundtrip(dense_reader operation, std::size_t row) {
    return consume_dense(pass_dense_reader(operation), row);
}
word erased_dense_roundtrip(erased_series_reader operation, std::size_t row) {
    return consume_erased_dense(pass_erased_series_reader(operation), row);
}
} // namespace hb
