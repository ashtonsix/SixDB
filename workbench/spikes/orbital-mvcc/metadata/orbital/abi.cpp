// Deliberately ordinary external boundaries, without LTO or trivial_abi.
#include <orbital/mvcc_baseline.h>

namespace o = orbital::object;
using orbital::Result;

extern "C" {
o::BorrowedContext packet_borrow(const o::Store& s) { return s.borrow(); }
Result<o::ReadContext> packet_read(const o::Store& s) { return s.read(); }
Result<o::Draft> packet_edit(const o::Store& s) { return s.edit(); }
Result<o::Prepared> packet_prepare(o::Store& s, o::Draft& d) { return s.prepare(d); }
Result<void> packet_install(o::Store& s, o::Prepared& p) { return s.install(p); }
Result<o::Selection> packet_select(const o::BorrowedContext& c, orbital::ObjectId id) {
  return o::select(c, id);
}
Result<o::View> packet_part(o::Selection s, std::size_t index) { return s.part(index); }
orbital::Bytes packet_bytes(o::View v) { return v.body(); }
o::RetainedView packet_retain_part(o::View v) { return o::retain(v); }
o::RetainedSelection packet_retain_selection(o::Selection s) { return o::retain(s); }
Result<orbital::MutableBytes> packet_writable(const o::WriteView& v) { return v.bytes(); }
o::Statistics packet_statistics(const o::Store& s) { return s.statistics(); }
}
