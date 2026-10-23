#pragma once
#include <ikea/seriespack/read.h>
#include <ikea/seriespack/write.h>
#include <ikea/tuplepack/author/execution.h>
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>

#define HB_NOINLINE [[gnu::noinline]]
#ifndef HB_PREPARED_PREDICATE
#define HB_PREPARED_PREDICATE 0
#endif

namespace hb {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
using word = std::uint64_t;
using reader = tp::reader<8, const std::byte>;
using writer = tp::writer<8>;
using native_reader = tp::author::native_reader<8, const std::byte>;
using native_writer = tp::author::native_writer<8>;
using filter_reader = tp::reader<64, const std::byte, 16>;
using predicate = tp::detail::field_predicate<64, const std::byte, 16>;
using filter = std::conditional_t<HB_PREPARED_PREDICATE, predicate, filter_reader>;
using read_plan = tp::read_plan<8>;
using write_plan = tp::write_plan<8>;
using filter_plan = tp::read_plan<64, 16>;
using erased_writer = tp::erased_writer<tp::packet<8>>;
using series_layout = sp::layout<31, sp::geometry::local, 16>;
using series_view = sp::view<series_layout>;
using series_expression = decltype(sp::author::expression(std::declval<const series_view&>()));
using series_reader = sp::reader<series_expression>;
using series_writer = sp::writer<series_expression>;
using dense_layout = sp::layout<31>;
using dense_source = sp::dense_source<dense_layout>;
using dense_reader = sp::reader<dense_source>;
using erased_series_reader = sp::erased_reader<word>;
using erased_series_writer = sp::erased_writer<word, sp::author::sum_change>;

#define HB_TUPLE_RAW_TYPES(X) \
    X(reader, reader) X(writer, writer) X(native_reader, native_reader) \
    X(native_writer, native_writer) X(filter_reader, filter_reader) \
    X(predicate, predicate) X(erased_writer, erased_writer)
#define HB_SERIES_RAW_TYPES(X) \
    X(dense_reader, dense_reader) X(series_reader, series_reader) \
    X(series_writer, series_writer) X(erased_series_reader, erased_series_reader) \
    X(erased_series_writer, erased_series_writer)
#define HB_DECLARE_PASS(name, type) HB_NOINLINE type pass_##name(type value);
HB_TUPLE_RAW_TYPES(HB_DECLARE_PASS)
HB_SERIES_RAW_TYPES(HB_DECLARE_PASS)
#undef HB_DECLARE_PASS

HB_NOINLINE std::expected<reader, tp::error> make_reader(const read_plan&, const tp::const_view&);
HB_NOINLINE std::expected<writer, tp::error> make_writer(const write_plan&, const tp::view&);
HB_NOINLINE std::expected<filter, tp::error> make_filter(const filter_plan&, const tp::const_view&);
HB_NOINLINE std::expected<void, tp::error> rebind_reader(reader&, const read_plan&, const tp::const_view&);
HB_NOINLINE word consume_reader(reader, std::size_t);
HB_NOINLINE std::expected<void, tp::error> consume_writer(writer, std::size_t, word, ikea::journal&);
HB_NOINLINE word consume_filter(filter, std::size_t, word, std::uint64_t);
HB_NOINLINE void store_reader(std::span<reader>, std::size_t, reader);

word raw_roundtrip(reader, std::size_t);
std::expected<word, tp::error> checked_roundtrip(const read_plan&, const tp::const_view&, std::size_t);
word stored_roundtrip(std::span<reader>, std::size_t, reader, std::size_t);
std::expected<void, tp::error> write_roundtrip(writer, std::size_t, word, ikea::journal&);
std::expected<word, tp::error> checked_filter_command(const filter_plan&, const tp::const_view&,
                                                    std::size_t, word, std::uint64_t);
word crowded_readers(const std::array<reader, 7>&, std::size_t,
                     word, word, word, word, word, word);
word crowded_filters(const std::array<filter, 7>&, std::size_t, word, std::uint64_t,
                     word, word, word, word, word, word);

// Named traits are emitted as literal LLVM constants. They describe layout,
// not ABI classification; the independent caller/callee objects establish ABI.
#define HB_CENSUS_TYPES(X) \
    HB_TUPLE_RAW_TYPES(X) HB_SERIES_RAW_TYPES(X) \
    X(read_plan, read_plan) X(write_plan, write_plan) X(tuple_view, tp::view) \
    X(series_view, series_view) \
    X(checked_reader, std::expected<reader, tp::error>) \
    X(checked_writer, std::expected<writer, tp::error>) \
    X(checked_filter, std::expected<filter, tp::error>) \
    X(checked_series_reader, std::expected<erased_series_reader, sp::error>)
} // namespace hb
