#pragma once
#if CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/native.h"
#include "workbench/benchmarks/orbital/versioned/consumer.h"
#else
#include "consumer/native.h"
#include "consumer/consumer.h"
#endif
#include <ikea/tuplepack/transfer.h>
#include <ikea/seriespack/author/write.h>
#include <type_traits>

// Probe the frozen 7f73c81 types, without changing their ABI or implementation.
// The runner supplies the captured source root ahead of the live checkout.
namespace consumer_metadata {
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
namespace sp = ikea::seriespack;
template<class T> using OR = orbital::Result<T>;
template<class T> using TR = std::expected<T, tp::error>;
template<class T> using SR = std::expected<T, sp::error>;
template<unsigned K> using SV = sp::view<v::Column<K>>;
template<unsigned K> using SWV = sp::view<v::Column<K>, std::uint8_t>;
template<unsigned K> using SP = typename decltype(sp::bind_mutation(
    std::declval<const SWV<K>&>()))::value_type;
using TupleRead = tp::read_operation<8, const tp::byte>;
using TupleNativeRead = tp::native_reader<8, const tp::byte>;
using TupleWrite = tp::mutation_operation<8>;
using TupleNativeWrite = tp::native_writer<8>;
using TupleTransfer = tp::unit_transfer<23, const tp::byte>;
using OptionalRecord = std::optional<orbital_bench::Record>;
using TupleCodes = std::array<tp::code, 23>;
using ParentBytes = std::array<std::byte, 24>;
using LogicalBytes = std::array<std::byte, 8>;
using OwnerEffects32 = std::array<ikea::owner_write, 32>;
using OwnerEffects128 = std::array<ikea::owner_write, 128>;
using SeriesStreams = std::array<sp::detail::stream_extent, 3>;
}

// Each type gets independent size/triviality and ordinary by-value ABI probes.
// API-unused neighboring carriers are labeled in the census, not treated hot.
#define CONSUMER_METADATA_TYPES(X) \
 X(TupleCursor, v::TupleCursor) \
 X(ColumnCursor64, v::ColumnCursor<64>) \
 X(ColumnCursor20, v::ColumnCursor<20>) \
 X(ColumnCursor32, v::ColumnCursor<32>) \
 X(ColumnCursors, v::ColumnCursors) \
 X(TupleControl, v::TupleControl) \
 X(ColumnControl, v::ColumnControl) \
 X(TupleBody, v::TupleBody) \
 X(ColumnBody, v::ColumnBody) \
 X(ReadData, v::ReadData) \
 X(Schema, v::Schema) \
 X(ReadPart, v::ReadPart) \
 X(WritePart, v::WritePart) \
 X(PartShape, v::PartShape) \
 X(Shape, v::Shape) \
 X(KernelCosts, v::KernelCosts) \
 X(Costs, v::Costs) \
 X(ConstructionOptions, v::ConstructionOptions) \
 X(Update, v::Update) \
 X(Record, orbital_bench::Record) \
 X(Row, orbital_bench::Row) \
 X(Operation, orbital_bench::Operation) \
 X(Outcome, orbital_bench::Outcome) \
 X(OptionalRecord, OptionalRecord) \
 X(Bytes, orbital::Bytes) \
 X(MutableBytes, orbital::MutableBytes) \
 X(PartId, orbital::PartId) \
 X(Description, orbital::object::Description) \
 X(RepresentationSpec, orbital::object::RepresentationSpec) \
 X(PartPlan, orbital::object::PartPlan) \
 X(PartSource, orbital::object::PartSource) \
 X(ObjectView, orbital::object::View) \
 X(ObjectSelection, orbital::object::Selection) \
 X(ObjectRepresentation, orbital::object::Representation) \
 X(ObjectWriteView, orbital::object::WriteView) \
 X(ObjectBuilder, orbital::object::Builder) \
 X(TupleLayout, tp::layout) \
 X(TupleCode, tp::code) \
 X(TupleCodes, TupleCodes) \
 X(TupleOrdering8, tp::packet_layout<8>) \
 X(TupleOrdering64, tp::packet_layout<64>) \
 X(TupleGprRead, tp::detail::gpr_read) \
 X(TupleGprWrite, tp::detail::gpr_write) \
 X(TupleWordTransfer, tp::detail::word_transfer) \
 X(TupleReader8, tp::reader<8>) \
 X(TupleWriter8, tp::writer<8>) \
 X(TupleWriter64, tp::writer<64>) \
 X(TupleConstructor, tp::constructor) \
 X(TupleConstructionInput, tp::construction_input) \
 X(TupleConstructionOperation, tp::construction_operation) \
 X(TupleView, tp::view) \
 X(TupleConstView, tp::const_view) \
 X(TupleRead, TupleRead) \
 X(TupleNativeRead, TupleNativeRead) \
 X(TupleWrite, TupleWrite) \
 X(TupleNativeWrite, TupleNativeWrite) \
 X(TupleTransfer, TupleTransfer) \
 X(SeriesRepresentation, sp::representation) \
 X(SeriesDescriptor, sp::descriptor_bytes) \
 X(SeriesPlane, sp::plane<const std::uint8_t>) \
 X(SeriesWritePlane, sp::plane<std::uint8_t>) \
 X(SeriesView64, SV<64>) \
 X(SeriesView20, SV<20>) \
 X(SeriesView32, SV<32>) \
 X(SeriesWriteView64, SWV<64>) \
 X(SeriesWriteView20, SWV<20>) \
 X(SeriesWriteView32, SWV<32>) \
 X(SeriesDense64, sp::dense_source<v::Column<64>>) \
 X(SeriesDense20, sp::dense_source<v::Column<20>>) \
 X(SeriesDense32, sp::dense_source<v::Column<32>>) \
 X(SeriesDecoder, sp::decoder<std::uint64_t>) \
 X(SeriesDenseInput, sp::dense_input) \
 X(SeriesStreamExtent, sp::detail::stream_extent) \
 X(SeriesStreams, SeriesStreams) \
 X(OccupiedRun, ikea::detail::occupied_run) \
 X(SeriesPrepared64, SP<64>) \
 X(SeriesPrepared20, SP<20>) \
 X(SeriesPrepared32, SP<32>) \
 X(SeriesSelection, sp::row_selection) \
 X(TupleSelection, tp::selection) \
 X(ByteWrite, ikea::byte_write) \
 X(OwnerWrite, ikea::owner_write) \
 X(WriteJournal, ikea::write_journal) \
 X(SourceWriteJournal, ikea::source_write_journal) \
 X(OwnerEffects32, OwnerEffects32) \
 X(OwnerEffects128, OwnerEffects128) \
 X(ParentBytes, ParentBytes) \
 X(LogicalBytes, LogicalBytes) \
 X(ResultTupleCursor, OR<v::TupleCursor>) \
 X(ResultColumnCursors, OR<v::ColumnCursors>) \
 X(ResultReadData, OR<v::ReadData>) \
 X(ResultShape, OR<v::Shape>) \
 X(ResultRecord, OR<orbital_bench::Record>) \
 X(ResultRow, OR<orbital_bench::Row>) \
 X(ResultVoid, OR<void>) \
 X(ResultView, OR<orbital::object::View>) \
 X(ResultRepresentation, OR<orbital::object::Representation>) \
 X(ResultWriteView, OR<orbital::object::WriteView>) \
 X(ResultBuilder, OR<orbital::object::Builder>) \
 X(ExpectedTupleLayout, TR<tp::layout>) \
 X(ExpectedTupleReader8, TR<tp::reader<8>>) \
 X(ExpectedTupleWriter8, TR<tp::writer<8>>) \
 X(ExpectedTupleConstructor, TR<tp::constructor>) \
 X(ExpectedTupleView, TR<tp::view>) \
 X(ExpectedTupleRead, TR<TupleRead>) \
 X(ExpectedTupleWrite, TR<TupleWrite>) \
 X(ExpectedTupleConstruction, TR<tp::construction_operation>) \
 X(ExpectedSeriesRepresentation, SR<sp::representation>) \
 X(ExpectedSeriesView20, SR<SV<20>>) \
 X(ExpectedSeriesDense20, SR<sp::dense_source<v::Column<20>>>) \
 X(ExpectedSeriesDecoder, SR<sp::decoder<std::uint64_t>>) \
 X(ExpectedSeriesPrepared20, SR<SP<20>>)
