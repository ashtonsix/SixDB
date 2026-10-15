#include "hash.h"
#include <random>
#include <unordered_set>

namespace {
using namespace ht;
struct summary {
    std::array<word,2> hit_checks{},miss_checks{};
    unsigned primary=0,secondary=0,overflow=0,worst_hit=0,worst_miss=0;
    std::vector<std::pair<unsigned,word>> placement;
};
struct entry {word suffix,key;byte fp;};
constexpr std::array names{"consecutive","gray","reverse","stride","fixed_prefix",
    "carry_boundary","rotated_counter","random","remainder_0x44",
    "suffix_consecutive","suffix_stride","suffix_carry"};

template<unsigned K,unsigned A> std::vector<word> inputs(unsigned n,unsigned pattern) {
    std::vector<word> result;result.reserve(2*n);
    std::mt19937_64 random(0x81813479ULL+K*113+A*317);
    std::unordered_set<word> seen;
    const unsigned bits=std::bit_width(2*n-1),shift=std::min(8u,K-bits);
    constexpr unsigned F=std::min(8u,K-A),C=K-A-F;
    for(word i=0;i<2*n;++i) {
        word key;
        switch(pattern) {
            case 0:key=i;break;
            case 1:key=i^(i>>1);break;
            case 2:key=__builtin_bitreverse64(i)>>(64-K);break;
            case 3:key=i<<shift;break;
            case 4:key=(mask(K)^mask(bits))|i;break;
            case 5:key=(i+(word(1)<<(K/2))-n)&mask(K);break;
            case 6:key=((i<<8)|(i>>(K-8)))&mask(K);break;
            case 7:do {key=random()&mask(K);}while(!seen.insert(key).second);break;
            case 8:key=((i>>A)*0x44ULL<<A)|(i&mask(A));break;
            default: {
                // Fixed low remainder, complete low-A coverage for each suffix.
                // Keys do not depend on the hash seed. The second half uses
                // held-out suffixes, including a carry through the top bit.
                if constexpr(C>=5) {
                    word suffix=i>>A;
                    if(pattern==10)suffix<<=std::min(C-5,8u);
                    if(pattern==11)suffix=(suffix+(word(1)<<(C-1))-12)&mask(C);
                    key=(suffix<<(A+F))|(i&mask(A));
                }else {require(false,"suffix cohort needs five suffix bits");key=0;}
                break;
            }
        }
        require(key<=mask(K),"quality input domain");result.push_back(key);
    }
    auto sorted=result;std::sort(sorted.begin(),sorted.end());
    require(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"quality input uniqueness");
    return result;
}

// Logical suffix-verification counts from the actual primary/spill placement.
// Chains are counted as a separate population; their full-key search work is
// not included in these bucket-only counters. Stop each bucket walk at success.
template<unsigned K,unsigned A,bool Multiply> summary measure(const std::vector<word>& keys,unsigned slots,word seed) {
    constexpr unsigned B=A>4?A-4:1;
    permutation<K,A,Multiply> primary(seed);
    permutation<K,B,Multiply> secondary(mix(seed^0x91817731eafULL));
    std::vector<std::array<entry,32>> p(word(1)<<A),q(word(1)<<B);
    std::vector<unsigned> pc(p.size()),qc(q.size()),descendants(p.size());
    summary out;const unsigned n=keys.size()/2;out.placement.reserve(n);
    for(unsigned i=0;i<n;++i) {
        const word key=keys[i];auto h=primary.split(key);
        require(primary.invert(h)==key,"primary inverse in placement history");
        if(pc[h.bucket]<slots) {
            p[h.bucket][pc[h.bucket]++]={h.suffix,key,h.fingerprint};
            ++out.primary;out.placement.emplace_back(0,h.bucket);
        }else {
            ++descendants[h.bucket];
            auto h2=secondary.split(key);require(secondary.invert(h2)==key,"secondary inverse in spill history");
            if(qc[h2.bucket]<slots) {
                q[h2.bucket][qc[h2.bucket]++]={h2.suffix,key,h2.fingerprint};
                ++out.secondary;out.placement.emplace_back(1,h2.bucket);
            }else {++out.overflow;out.placement.emplace_back(2,h2.bucket);}
        }
    }
    auto probe=[](const auto& bucket,unsigned count,auto h,word key,unsigned& checks) {
        for(unsigned i=0;i<count;++i)if(bucket[i].fp==h.fingerprint) {
            ++checks;
            if(bucket[i].suffix==h.suffix){require(bucket[i].key==key,"suffix match reconstructs exact key");return true;}
        }
        return false;
    };
    for(unsigned i=0;i<2*n;++i) {
        std::array<unsigned,2> checks{};const auto h=primary.split(keys[i]);
        bool found=probe(p[h.bucket],pc[h.bucket],h,keys[i],checks[0]);
        if(!found && descendants[h.bucket]) {
            const auto h2=secondary.split(keys[i]);
            found=probe(q[h2.bucket],qc[h2.bucket],h2,keys[i],checks[1]);
        }
        require(found==(i<n && out.placement[i].first!=2),"bucket point-result oracle");
        for(unsigned tier=0;tier<2;++tier) {
            if(i<n)out.hit_checks[tier]+=checks[tier];else out.miss_checks[tier]+=checks[tier];
        }
        const auto total=checks[0]+checks[1];
        if(i<n)out.worst_hit=std::max(out.worst_hit,total);
        else out.worst_miss=std::max(out.worst_miss,total);
    }
    return out;
}

template<unsigned K,unsigned A> void geometry(unsigned seeds) {
    for(unsigned slots:{8u,16u,24u})for(unsigned pattern=0;pattern<names.size();++pattern) {
        unsigned n=std::min<word>((word(1)<<A)*slots*9/10,word(1)<<std::min(K-1,16u));
        if(pattern==8) {
            n=std::min(n,12u<<A);
            if((((word(2)*n-1)>>A)*0x44ULL)>mask(K-A))continue;
        }
        if(pattern>=9) {
            if constexpr(permutation<K,A,true>::C<5)continue;
            n=12u<<A;
        }
        const auto keys=inputs<K,A>(n,pattern);
        for(unsigned i=0;i<seeds;++i) {
            const auto seed=mix(i+0x66129453812ULL);
            const auto control=measure<K,A,false>(keys,slots,seed);
            const auto candidate=measure<K,A,true>(keys,slots,seed);
            require(control.placement==candidate.placement && control.primary==candidate.primary &&
                control.secondary==candidate.secondary && control.overflow==candidate.overflow,"selector preserves full tier placement");
            unsigned mode=0;
            for(const auto* s:{&control,&candidate}) {
                const auto hits_in_buckets=n-s->overflow;
                require(s->hit_checks[0]>=s->primary && s->hit_checks[1]>=s->secondary,"each bucket hit confirms its own suffix");
                constexpr unsigned B=A>4?A-4:1;
                std::printf("%u,%u,%u,%u,%s,%u,%u,%u,%s,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u\n",K,A,slots,n,names[pattern],i,
                    permutation<K,A,true>::selector_word_bits,permutation<K,B,true>::selector_word_bits,
                    mode++?"multiply":"fold",s->primary,s->secondary,s->overflow,
                    (unsigned long long)(s->hit_checks[0]+s->hit_checks[1]-hits_in_buckets),
                    (unsigned long long)(s->miss_checks[0]+s->miss_checks[1]),
                    (unsigned long long)(s->hit_checks[0]-s->primary),(unsigned long long)(s->hit_checks[1]-s->secondary),
                    (unsigned long long)s->miss_checks[0],(unsigned long long)s->miss_checks[1],s->worst_hit,s->worst_miss);
            }
        }
    }
}
template<unsigned K> void domain(unsigned seeds) {
    geometry<K,3>(seeds);geometry<K,6>(seeds);geometry<K,7>(seeds);
    if constexpr(K>8){geometry<K,8>(seeds);geometry<K,10>(seeds);}
}
}
int main(int argc,char** argv) {
    const unsigned seeds=argc>1?std::strtoul(argv[1],nullptr,10):64;require(seeds>0,"quality seeds");
    std::puts("K,A,slots,n,pattern,seed,primary_selector_word_bits,secondary_selector_word_bits,variant,primary,secondary,overflow,extra_hit_suffix_checks,miss_suffix_checks,primary_extra_hit_checks,secondary_extra_hit_checks,primary_miss_checks,secondary_miss_checks,worst_hit_suffix_checks,worst_miss_suffix_checks");
    domain<8>(seeds);domain<16>(seeds);domain<24>(seeds);domain<32>(seeds);
    domain<40>(seeds);domain<48>(seeds);domain<56>(seeds);domain<64>(seeds);
}
