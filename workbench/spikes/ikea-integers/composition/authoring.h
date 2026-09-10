#pragma once
#include "../formats.h"

namespace ikea::integers::composition {
enum class TailKind { local4, scan4 };
// Enclosing physical placement is an independent choice. In particular Scan4
// supports both body-first and tail-middle arrangements without changing bytes
// within the tail child. Compatibility is checked by preparation.
enum class ParentKind { packets8, body64_tail32, body32_tail32_body32 };
struct Body8 { static constexpr const char* contract="high8-of-u12"; };
struct Local4 {
    using format=LocalPack<4>;
    static constexpr TailKind kind=TailKind::local4;
    static constexpr const char* contract="local4-transpose8";
};
struct Scan4 {
    using format=ScanPack<4>;
    static constexpr TailKind kind=TailKind::scan4;
    static constexpr const char* contract="scan4-chunks32";
};
template<class Tail> struct Payload12 { Body8 body; Tail tail; };
template<class Tail> struct Column {
    std::uint64_t source_id;
    Payload12<Tail> values;
    // Substituting this logical child can require another enclosing placement,
    // which the caller chooses explicitly. Body bytes move too in this probe:
    // actual_source names a fully re-encoded payload, not an in-place tail edit.
    template<class OtherTail> constexpr Column<OtherTail> with_tail(OtherTail,std::uint64_t actual_source) const {
        return {actual_source,{values.body,{}}};
    }
};

// Both the recorder and native executor consume these same authored bodies.
// Child identity and the enclosing placement remain distinct from a kernel.
template<class Ops,class Tail,class Group>
auto read12(Ops& ops,const Payload12<Tail>& source,Group group) {
    auto high=ops.read_body(source.body,group);
    auto low=ops.read_tail(source.tail,group);
    return ops.join12(high,low);
}
template<class Ops,class Tail,class Group,class Cutoff>
auto filtered_sum16(Ops& ops,const Column<Tail>& source,Group group,Cutoff cutoff) {
    auto values=read12(ops,source.values,group);
    auto selected=ops.less_than(values,cutoff);
    return ops.masked_sum(values,selected);
}
} // namespace ikea::integers::composition
