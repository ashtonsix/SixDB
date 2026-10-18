#pragma once
// Included by bags.h, after the StreamPack headers and namespace aliases.
namespace iceberg {
// One permanent fixed StreamPack rank per primary slot. Index membership owns
// liveness; replacing a private slot does not retire its permanent reservation.
// A concurrent owner must preserve old versions and publish the payload damage.
template<unsigned V,unsigned N,std::size_t Buckets,class Storage=orbital_storage_policy> class primary_bags {
    using allocation_context=typename Storage::context;
    using allocation=typename Storage::buffer;
    static_assert(N<=stream::slot_capacity);
    static constexpr std::size_t payload=N*V,stride=(payload+2+63)&~std::size_t{63};
    stream::layout format_=[] {
        const auto f=stream::layout::prepare({.family=stream::geometry::direct,.maximum=V,
            .byte_limit=payload,.fixed_reservation=V,.full=true,.immortal=true});
        require(bool(f),"primary StreamPack layout");
        require(f->control_bytes()==2&&!f->directory_bytes()&&!f->lengths_bytes(),"fixed block recipe");
        return *f;
    }();
    static std::size_t storage_bytes(){
        if(Buckets>std::numeric_limits<std::size_t>::max()/stride)throw std::length_error("primary bag extent");
        return Buckets*stride;
    }
    allocation storage_;
    primary_bags(const primary_bags& source,allocation_context* memory):
        format_(source.format_),storage_(duplicate_buffer<Storage>(source.storage_,memory)){}
    auto view_at(std::size_t offset)const {
        auto* p=reinterpret_cast<stream::byte*>(storage_.data+offset);
        return stream::view::assume_valid(format_,{{p,payload},{},{},{p+payload,2}});
    }
    auto view(word bucket)const{return view_at(bucket*stride);}
  public:
    primary_bags clone(allocation_context* memory=nullptr)const{return primary_bags(*this,memory);}
    explicit primary_bags(allocation_context* memory=nullptr):storage_(storage_bytes(),memory){
        require(Buckets<=((word{1}<<54)-1)/(stride/64),"primary StreamPack block ID width");
        const bytes<V> zero{};ikea::no_coverage effects;
        for(word bucket=0;bucket<Buckets;++bucket){auto dst=view(bucket);
            require(bool(stream::initialize(dst,effects)),"primary StreamPack initialization");
            for(unsigned rank=0;rank<N;++rank)
                require(bool(stream::append(dst,std::as_bytes(std::span(zero)),V,effects)),"primary StreamPack ranks");
        }
    }
    static word reference(word bucket,unsigned rank){return ((bucket*(stride/64))<<8)|rank;}
    template<class Emit> void write_damage(word bucket,unsigned rank,Emit&& emit)const {
        emit(mutation_damage{{this,owner_part::associated_bags,storage_.data,storage_.size},
            {0,bucket*stride+rank*V,V}});
    }
    word write(word bucket,unsigned rank,const bytes<V>& value){auto dst=view(bucket);ikea::no_coverage effects;
        require(bool(stream::replace(dst,rank,std::as_bytes(std::span(value)),effects)),"primary StreamPack replacement");
        return reference(bucket,rank);
    }
    [[gnu::always_inline]] stream::bytes locate(word bucket,unsigned rank)const {
        const bool fixed=format_.fixed(),used=format_.has_used();const auto width=format_.fixed_width();
        __builtin_assume(fixed);__builtin_assume(!used);__builtin_assume(width==V);
        const auto source=view(bucket);
        const stream::author::access<stream::geometry::direct,std::uint8_t,std::uint32_t,stream::byte> read(source);
        return read.value(rank);
    }
    [[gnu::always_inline]] stream::bytes locate(word ref)const{return locate((ref>>8)/(stride/64),unsigned(ref&255));}
    template<class Consume> [[gnu::always_inline]] void consume(word ref,Consume&& consume_value)const {
        const auto src=locate(ref);bytes<V> value;std::memcpy(value.data(),src.data(),V);consume_value(value);
    }
    std::vector<byte> copy(word ref)const {const auto src=locate(ref);std::vector<byte> out(V);std::memcpy(out.data(),src.data(),V);return out;}
    std::size_t bytes_used()const{return storage_.capacity()+sizeof(*this);}
    storage_usage storage()const{return storage_.usage();}
    void retain_storage(typename Storage::retention& holds)const{storage_.retain(holds);}
    typename Storage::trim_result relinquish_surplus(){return storage_.relinquish_surplus();}
    template<class Visit> void allocations(Visit&& visit)const {visit("associated_bags",storage_.data,storage_.size);}
    void validate()const {for(word b=0;b<Buckets;++b){const auto src=view(b);require(bool(src.validate())&&src.size()==N,"primary StreamPack contents");}}
    void validate_reference(word ref)const {
        require((ref>>8)%(stride/64)==0&&(ref>>8)/(stride/64)<Buckets&&(ref&255)<N,"primary StreamPack reference");
    }
};
struct no_primary_bags {
    template<class Context> explicit no_primary_bags(Context*){}
    template<class Context> no_primary_bags clone(Context* memory)const{return no_primary_bags(memory);}
};
}
