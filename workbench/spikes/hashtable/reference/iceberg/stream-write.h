#pragma once
#include <cstdint>
#include <type_traits>
#include <ikea/streampack/author/write.h>

namespace iceberg::bag_write {
namespace sp=ikea::streampack;
// Execution state is separate from the original named source. Effects always
// identify that original view; the immutable copy supplies schema calculations
// across payload memmove, whose accepted aliases must remain supported.
struct cursor {
    const sp::view* original;
    unsigned n,begin;
    unsigned size()const{return n;}
    unsigned extent()const{return begin;}
    const auto& windows()const{return original->windows();}
    operator const sp::view&()const{return *original;}
};
template<class Width,class Offset> struct append_access {
    static constexpr auto family=sp::geometry::direct;
    using width_type=Width;
    using offset_type=Offset;
    const cursor* state;
    const sp::layout* recipe;
    const cursor& source()const{return *state;}
    const sp::layout& format()const{return *recipe;}
    unsigned width_offset(unsigned rank)const{return (rank/16)*recipe->group_stride()+(rank%16)*sizeof(Width);}
};
// The arena constructor proves this exact Direct/fixed-or-U16 recipe and full
// metadata capacity. The caller admits the whole command, input and effects
// before entry, holds exclusive physical authority and keeps all borrowed
// controls stable/disjoint. This is only an append adapter, not Access::rebind.
template<unsigned V,bool Fixed,class Offset> class writer {
    const sp::layout snapshot_;
    cursor cursor_;
  public:
    [[gnu::always_inline]] writer(const sp::view& destination,unsigned n,unsigned begin)
        :snapshot_(destination.format()),cursor_{&destination,n,begin}{}
    template<class Effects> [[gnu::always_inline]] void append(sp::bytes input,unsigned reservation,Effects& effects) {
        const bool fixed=snapshot_.fixed(),used=snapshot_.has_used(),tombs=snapshot_.has_tombstones(),extent=snapshot_.has_extent();
        const auto width=snapshot_.fixed_width(),geometry=snapshot_.geometry_offset(),used_offset=snapshot_.used_offset(),stride=snapshot_.group_stride(),control=snapshot_.control_bytes();
        __builtin_assume(fixed==Fixed);__builtin_assume(used==!Fixed);
        if constexpr(Fixed)__builtin_assume(width==V);
        __builtin_assume(tombs);__builtin_assume(!extent);__builtin_assume(control==2);
        __builtin_assume(geometry==32);__builtin_assume(used_offset==0);__builtin_assume(stride==(Fixed?0:32));
        using Width=std::conditional_t<Fixed,std::uint8_t,std::uint16_t>;
        const append_access<Width,Offset> access{&cursor_,&snapshot_};
        [[clang::always_inline]] sp::author::append_unchecked(access,input,reservation,effects);
        ++cursor_.n;cursor_.begin+=reservation;
    }
    unsigned extent()const{return cursor_.begin;}
    template<class Effects> [[gnu::always_inline]] void append_run(sp::bytes input,unsigned count,Effects& effects) requires(Fixed) {
        const bool fixed=snapshot_.fixed(),used=snapshot_.has_used(),tombs=snapshot_.has_tombstones(),extent=snapshot_.has_extent();
        const auto width=snapshot_.fixed_width(),control=snapshot_.control_bytes();
        __builtin_assume(fixed);__builtin_assume(!used);__builtin_assume(tombs);
        __builtin_assume(!extent);__builtin_assume(width==V);__builtin_assume(control==2);
        const append_access<std::uint8_t,Offset> access{&cursor_,&snapshot_};
        if(count==1){
            [[clang::always_inline]] sp::author::append_fixed_run_unchecked(access,input.first(V),1,effects);
        }else{
            [[clang::always_inline]] sp::author::append_fixed_run_unchecked(access,input.first(2*V),2,effects);
        }
        cursor_.n+=count;cursor_.begin+=input.size();
    }
};
template<class Width> struct replacement_access {
    using width_type=Width;
    const sp::view* original;
    const sp::layout* recipe;
    unsigned begin;
    const sp::view& source()const{return *original;}
    const sp::layout& format()const{return *recipe;}
    unsigned start(unsigned)const{return begin;}
    unsigned width_offset(unsigned rank)const{return (rank/16)*recipe->group_stride()+(rank%16)*sizeof(Width);}
};
// The owner has admitted the live rank, input and existing reservation. The
// start is read before any effects; the original named view remains the source.
template<bool Fixed,class Effects> [[gnu::always_inline]] void replace(
        const sp::view& destination,unsigned rank,unsigned begin,sp::bytes input,Effects& effects) {
    const sp::layout snapshot=destination.format();
    const bool used=snapshot.has_used();const auto stride=snapshot.group_stride(),offset=snapshot.used_offset();
    __builtin_assume(used==!Fixed);__builtin_assume(stride==(Fixed?0:32));__builtin_assume(offset==0);
    using Width=std::conditional_t<Fixed,std::uint8_t,std::uint16_t>;
    const replacement_access<Width> access{&destination,&snapshot,begin};
    [[clang::always_inline]] sp::author::replace_unchecked(access,rank,input,effects);
}
template<unsigned V,class Effects> [[gnu::always_inline]] void replace_run(
        const sp::view& destination,unsigned rank,unsigned begin,sp::bytes input,Effects& effects) {
    const sp::layout snapshot=destination.format();
    const replacement_access<std::uint8_t> access{&destination,&snapshot,begin};
    if(input.size()==V){
        [[clang::always_inline]] sp::author::replace_fixed_run_unchecked(access,rank,input.first(V),effects);
    }else{
        [[clang::always_inline]] sp::author::replace_fixed_run_unchecked(access,rank,input.first(2*V),effects);
    }
}
struct extension_access : replacement_access<std::uint16_t> {
    unsigned used;
    unsigned length(unsigned)const{return used;}
};
template<class Effects> [[gnu::always_inline]] void extend(
        const sp::view& destination,unsigned rank,unsigned begin,unsigned used,sp::bytes input,Effects& effects) {
    const sp::layout snapshot=destination.format();
    const bool has_used=snapshot.has_used();const auto stride=snapshot.group_stride(),offset=snapshot.used_offset();
    __builtin_assume(has_used);__builtin_assume(stride==32);__builtin_assume(offset==0);
    const extension_access access{{&destination,&snapshot,begin},used};
    [[clang::always_inline]] sp::author::extend_unchecked(access,rank,input,effects);
}
}
