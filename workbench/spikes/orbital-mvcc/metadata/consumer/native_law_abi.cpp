#include "operations.h"
#include "compact_operations.h"
#include "canonical_law.h"
namespace c = orbital_mvcc::metadata_consumer;
using orbital_bench::Key;
using orbital_bench::Outcome;

// Compile separately for each law. This exposes emitted hot point/chain code
// and constants independently of cold factories. Object-section sizes are not
// application RSS, optimized caller frames or incremental linked code size.
#if CONSUMER_LAW_PROBE == 1
extern "C" void consumer_law_point8(const orbital_bench::versioned::TupleControl& control,
    c::QualifiedTuple8 state, Key key, Outcome& output) {
  c::read_point8_into(control, state, key, output);
}
extern "C" void consumer_law_chain8(const orbital_bench::versioned::TupleControl& control,
    c::QualifiedTuple8 state, Key key, Outcome& output) {
  c::read_chain8_into(control, state, key, 16, output);
}
#elif CONSUMER_LAW_PROBE == 2
extern "C" void consumer_law_point8(const c::compact_law::Control& control,
    c::compact_law::Cursor state, Key key, Outcome& output) {
  c::compact_law::read_point8_into(control, state, key, output);
}
extern "C" void consumer_law_chain8(const c::compact_law::Control& control,
    c::compact_law::Cursor state, Key key, Outcome& output) {
  c::compact_law::read_chain8_into(control, state, key, 16, output);
}
#elif CONSUMER_LAW_PROBE == 3
extern "C" void consumer_law_point8(const c::canonical_law::Control& control,
    c::canonical_law::Cursor state, Key key, Outcome& output) {
  c::canonical_law::read_point8_into(control, state, key, output);
}
extern "C" void consumer_law_chain8(const c::canonical_law::Control& control,
    c::canonical_law::Cursor state, Key key, Outcome& output) {
  c::canonical_law::read_chain8_into(control, state, key, 16, output);
}
#else
#error Select a full-Schema, compact or canonical law for this ordinary ABI probe.
#endif
