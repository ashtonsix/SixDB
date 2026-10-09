#include "descriptor_adapter.h"
#include "operations.h"
namespace c = orbital_mvcc::metadata_consumer;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
using orbital_bench::Key;
using orbital_bench::Outcome;

extern "C" orbital::Result<c::QualifiedTuple8> consumer_candidate_part8(
    const v::TupleControl& control, orbital::Bytes body, orbital::Bytes interpretation) {
  return c::qualify_tuple_part8(control, body, interpretation);
}
extern "C" orbital::Result<c::ExplicitTuple16> consumer_candidate_part16(
    const v::TupleControl& control, orbital::Bytes body, orbital::Bytes interpretation) {
  return c::qualify_tuple_part16(control, body, interpretation);
}
extern "C" orbital::Result<void> consumer_candidate_part16_into(
    const v::TupleControl& control, orbital::Bytes body, orbital::Bytes interpretation,
    c::ExplicitTuple16& output) {
  return c::qualify_tuple_part16(control, body, interpretation, output);
}
extern "C" orbital::Result<c::QualifiedTuple8> consumer_candidate_descriptor_view8(
    const v::TupleControl& control, cd::View view, orbital::PartId requested) {
  return c::qualify_borrowed_part8(control, view, requested);
}
extern "C" orbital::Result<c::QualifiedTuple8> consumer_candidate_descriptor_selected8(
    const v::TupleControl& control, cd::Selection selected, orbital::ObjectId object,
    std::uint64_t version, orbital::PartId requested) {
  return c::qualify_descriptor_tuple8(control, selected, object, version, requested);
}
extern "C" void consumer_candidate_point8(const v::TupleControl& control,
    c::QualifiedTuple8 state, Key key, Outcome& output) {
  c::read_point8_into(control, state, key, output);
}
extern "C" void consumer_candidate_chain8(const v::TupleControl& control,
    c::QualifiedTuple8 state, Key key, Outcome& output) {
  c::read_chain8_into(control, state, key, 16, output);
}
extern "C" void consumer_candidate_chain16(const v::Schema& schema,
    c::ExplicitTuple16 state, Key key, Outcome& output) {
  c::read_chain_into(schema, state, key, 16, output);
}
extern "C" orbital::Result<c::QualifiedTuple8> consumer_candidate_descriptor_borrowed8(
    const v::TupleControl& control, cd::Selection selected, orbital::ObjectId object,
    std::uint64_t version, orbital::PartId requested) {
  return c::qualify_descriptor_borrowed_tuple8(control, selected, object, version, requested);
}
