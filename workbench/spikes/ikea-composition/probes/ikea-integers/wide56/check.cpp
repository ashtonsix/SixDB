#include "api.h"
#include "kernels.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

using namespace ikea::integers::wide56;

namespace {
std::uint64_t random_state=0x582673acc801fe43ULL;
std::uint64_t random_word() {
    random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;
    return random_state;
}
std::uint64_t oracle_get(const std::uint8_t* p) {
    std::uint64_t v=0;
    for(unsigned b=0;b<7;++b) v|=std::uint64_t(p[b])<<(8*b);
    return v;
}
void oracle_encode(const std::uint64_t* values,std::uint8_t* p,unsigned n) {
    for(unsigned i=0;i<n;++i) for(unsigned b=0;b<7;++b) p[7*i+b]=std::uint8_t(values[i]>>(8*b));
}

std::size_t cases=0,points=0;
void check_block(const std::array<std::uint64_t,block_values>& values,unsigned offset) {
    std::array<std::uint8_t,block_bytes> expected;
    oracle_encode(values.data(),expected.data(),block_values);
    std::array<std::uint8_t,block_bytes+128> packed;
    packed.fill(0xa5);
    auto* p=packed.data()+offset+16;
    wide56_local_encode256(values.data(),p);
    assert(std::equal(expected.begin(),expected.end(),p));
    assert(std::all_of(packed.begin(),packed.begin()+offset+16,[](auto b) {return b==0xa5;}));
    assert(std::all_of(packed.begin()+offset+16+block_bytes,packed.end(),[](auto b) {return b==0xa5;}));
    std::array<std::uint64_t,block_values+2> decoded;
    decoded.fill(0xdeadbeefcafebabeULL);
    wide56_local_decode256(p,decoded.data()+1);
    assert(std::equal(values.begin(),values.end(),decoded.begin()+1));
    assert(decoded.front()==0xdeadbeefcafebabeULL && decoded.back()==0xdeadbeefcafebabeULL);
    std::uint64_t sum=0;
    for(unsigned i=0;i<block_values;++i) {
        assert(wide56_local_get1(p,i)==values[i]);
        assert(oracle_get(p+7*i)==values[i]);
        sum+=values[i];++points;
    }
    assert(wide56_local_sum256(p)==sum);
    for(unsigned i=0;i<block_values;i+=16) {
        std::array<std::uint64_t,18> group;
        group.fill(0xdeadbeefcafebabeULL);
        wide56_local_get16(p,i,group.data()+1);
        assert(std::equal(values.begin()+i,values.begin()+i+16,group.begin()+1));
        assert(group.front()==0xdeadbeefcafebabeULL && group.back()==0xdeadbeefcafebabeULL);
    }
    // Exercise the exposed native encoder as well as the selected parent
    // encoder. On NEON these deliberately have different store groupings.
    for(unsigned packet=0;packet<block_values/packet_values;++packet) {
        std::array<std::uint8_t,packet_bytes+2> native;
        native.fill(0xa5);
        unroll<fragments_per_packet>([&](auto part) {
            encode_fragment<part>(native.data()+1,load_values(values.data()+packet*packet_values+part*fragment_values));
        });
        assert(std::equal(native.begin()+1,native.end()-1,expected.begin()+packet*packet_bytes));
        assert(native.front()==0xa5 && native.back()==0xa5);
    }
    ++cases;
}

struct GuardedPage {
    std::size_t bytes=std::size_t(sysconf(_SC_PAGESIZE));
    std::uint8_t* mapping=static_cast<std::uint8_t*>(mmap(nullptr,3*bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    GuardedPage() {
        assert(mapping!=MAP_FAILED);
        assert(mprotect(begin(),bytes,PROT_READ|PROT_WRITE)==0);
    }
    ~GuardedPage() {assert(munmap(mapping,3*bytes)==0);}
    std::uint8_t* begin() {return mapping+bytes;}
    std::uint8_t* end() {return mapping+2*bytes;}
};
void check_guards() {
    GuardedPage packed,source,output;
    auto* values=reinterpret_cast<std::uint64_t*>(source.end()-block_values*sizeof(std::uint64_t));
    auto* decoded=reinterpret_cast<std::uint64_t*>(output.end()-block_values*sizeof(std::uint64_t));
    std::array<std::uint8_t,block_bytes> expected;
    for(unsigned i=0;i<block_values;++i) values[i]=random_word()&value_mask;
    oracle_encode(values,expected.data(),block_values);
    // Both edges: no read before the source and no load/store beyond the exact
    // packed or u64 extent. The end case also checks the last 56-byte packet.
    for(auto* p:{packed.begin(),packed.end()-block_bytes}) {
        wide56_local_encode256(values,p);
        assert(std::equal(expected.begin(),expected.end(),p));
        assert(mprotect(packed.begin(),packed.bytes,PROT_READ)==0);
        wide56_local_decode256(p,decoded);
        assert(std::equal(values,values+block_values,decoded));
        std::uint64_t sum=0;
        for(unsigned i=0;i<block_values;++i) {assert(wide56_local_get1(p,i)==values[i]);sum+=values[i];}
        assert(wide56_local_sum256(p)==sum);
        for(unsigned i=0;i<block_values;i+=16) {
            auto* group=reinterpret_cast<std::uint64_t*>(output.end()-16*sizeof(std::uint64_t));
            wide56_local_get16(p,i,group);
            assert(std::equal(values+i,values+i+16,group));
        }
        assert(mprotect(packed.begin(),packed.bytes,PROT_READ|PROT_WRITE)==0);
    }
    auto* packet=packed.end()-packet_bytes;
    std::array<std::uint64_t,packet_values> eight;
    std::copy_n(values,packet_values,eight.begin());
    oracle_encode(eight.data(),packet,packet_values);
    unroll<fragments_per_packet>([&](auto part) {
        std::array<std::uint64_t,fragment_values> fragment;
        store_values(fragment.data(),decode_fragment<part>(packet));
        assert(std::equal(fragment.begin(),fragment.end(),eight.begin()+part*fragment_values));
        encode_fragment<part>(packet,load_values(eight.data()+part*fragment_values));
    });
    std::array<std::uint8_t,packet_bytes> expected_packet;
    oracle_encode(eight.data(),expected_packet.data(),packet_values);
    assert(std::equal(expected_packet.begin(),expected_packet.end(),packet));
}
}

int main() {
    std::array<std::uint64_t,block_values> values{};
    check_block(values,0);
    values.fill(value_mask);check_block(values,63);
    for(unsigned bit=0;bit<packet_values*56;++bit) {
        values.fill(0);
        for(unsigned packet=0;packet<block_values/packet_values;++packet)
            values[packet*packet_values+bit/56]=std::uint64_t(1)<<(bit%56);
        check_block(values,bit%64);
    }
    for(unsigned offset=0;offset<64;++offset) for(unsigned trial=0;trial<4;++trial) {
        for(auto& value:values) value=random_word()&value_mask;
        check_block(values,offset);
    }
    check_guards();
    std::printf("wide56_cases,point_reads,native_fragment_values\n%zu,%zu,%u\n",cases,points,fragment_values);
}
