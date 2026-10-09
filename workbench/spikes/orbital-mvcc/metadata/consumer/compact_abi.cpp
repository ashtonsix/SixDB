#include "compact_descriptor_adapter.h"
#include "compact_operations.h"
#include "canonical_descriptor_adapter.h"
namespace c = orbital_mvcc::metadata_consumer;
namespace law = c::compact_law;
namespace fixed = c::canonical_law;
namespace cd = compact_descriptor;
using orbital_bench::Key;
using orbital_bench::Outcome;

extern "C" law::ReadLaw consumer_compact_read_law(law::ReadLaw value) { return value; }
extern "C" law::Control consumer_compact_control(law::Control value) { return value; }
extern "C" fixed::Control consumer_canonical_control(fixed::Control value) { return value; }
extern "C" law::Cursor consumer_compact_cursor(law::Cursor value) { return value; }
extern "C" law::Part consumer_compact_part(law::Part value) { return value; }
extern "C" fixed::Cursor consumer_canonical_cursor(fixed::Cursor value) { return value; }
extern "C" fixed::Part consumer_canonical_part(fixed::Part value) { return value; }
extern "C" orbital::Result<law::Cursor> consumer_compact_qualify8(
    const law::Control& control, orbital::Bytes body, orbital::Bytes interpretation) {
  return law::qualify_cursor(control, body, interpretation);
}
extern "C" orbital::Result<law::Part> consumer_compact_qualify16(
    const law::Control& control, orbital::Bytes body, orbital::Bytes interpretation) {
  return law::qualify_part(control, body, interpretation);
}
extern "C" orbital::Result<fixed::Cursor> consumer_canonical_qualify8(
    const fixed::Control& control, orbital::Bytes body, orbital::Bytes interpretation) {
  return fixed::qualify_cursor(control, body, interpretation);
}
extern "C" orbital::Result<fixed::Part> consumer_canonical_qualify16(
    const fixed::Control& control, orbital::Bytes body, orbital::Bytes interpretation) {
  return fixed::qualify_part(control, body, interpretation);
}
extern "C" orbital::Result<law::Cursor> consumer_compact_selected8(
    const law::Control& control, cd::Selection selected, orbital::ObjectId object,
    std::uint64_t version, orbital::PartId requested) {
  return law::qualify_descriptor_cursor(control, selected, object, version, requested);
}
extern "C" orbital::Result<fixed::Cursor> consumer_canonical_selected8(
    const fixed::Control& control, cd::Selection selected, orbital::ObjectId object,
    std::uint64_t version, orbital::PartId requested) {
  return fixed::qualify_descriptor_cursor(control, selected, object, version, requested);
}
extern "C" void consumer_compact_point8(const law::Control& control,
    law::Cursor state, Key key, Outcome& output) {
  law::read_point8_into(control, state, key, output);
}
extern "C" void consumer_compact_chain8(const law::Control& control,
    law::Cursor state, Key key, Outcome& output) {
  law::read_chain8_into(control, state, key, 16, output);
}
extern "C" void consumer_compact_chain16(const law::ReadLaw& read_law,
    law::Part state, Key key, Outcome& output) {
  law::read_chain_into(read_law, state, key, 16, output);
}
extern "C" void consumer_canonical_point8(const fixed::Control& control,
    fixed::Cursor state, Key key, Outcome& output) {
  fixed::read_point8_into(control, state, key, output);
}
extern "C" void consumer_canonical_chain8(const fixed::Control& control,
    fixed::Cursor state, Key key, Outcome& output) {
  fixed::read_chain8_into(control, state, key, 16, output);
}
extern "C" void consumer_canonical_chain16(fixed::Part state, Key key, Outcome& output) {
  fixed::read_chain_into(state, key, 16, output);
}
