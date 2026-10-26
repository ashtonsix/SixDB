// Production author::stage defaults, including request construction, over a
// decoded-only leaf. The original span, first policy and final policy use the
// identical driver and oracle. This isolates the density decision from codecs.
#include <ikea/frame/author/stage.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
namespace fr = ikea::frame;
using u64 = std::uint64_t;
class decoded final : public fr::author::stage {
    std::vector<u64> values_;
public:
    std::expected<void,fr::error> fit(unsigned u,const fr::author::fit_input& in) override {
        sized(u,in.values.size()); values_.assign(in.values.begin(),in.values.end()); return {};
    }
    void decode(unsigned u,unsigned b,u64* out) const override {
        std::copy_n(values_.data()+b*fr::block_rows,rows_in(u,b),out);
    }
};
volatile u64 sink;
int main(int argc,char**argv) {
    if(argc!=2) return 2;
    std::mt19937_64 rng(157);
    std::vector<u64> values(4096+200);
    for(auto& v:values) v=rng();
    values[0]=~u64{0}; values[63]=0;
    decoded leaf;
    if(!leaf.fit(0,{values})) return 1;
    for(unsigned density: {0u,1u,16u,64u,85u,96u,103u,115u,128u,160u,192u,255u,256u}) {
        fr::block_rows_set mask{};
        for(unsigned b=0;b<leaf.blocks_in(0);++b)
            for(unsigned r=0;r<leaf.rows_in(0,b);++r)
                mask[b].set(r,rng()%256<density);
        const auto fine=ikea::seriespack::selection::bits(0,std::span<const ikea::bitset256>(mask));
        for(unsigned first: {0u,17u}) {
            const unsigned count=unsigned(values.size())-first-13;
            fr::summary want;
            for(unsigned r=first;r<first+count;++r) if(mask[r/256].contains(r%256)) {
                const auto v=values[r];want.sum+=v;++want.count;want.min=std::min(want.min,v);want.max=std::max(want.max,v);
            }
            const auto run=[&](bool fused) {
                const auto q=fr::author::request::make(unsigned(values.size()),first,count,leaf.blocks(0),fine);
                fr::summary got;
                if(fused) leaf.summarise(0,q,got); else got.sum=leaf.sum(0,q);
                return got;
            };
            for(bool fused: {false,true}) {
                const auto got=run(fused);
                if(got.sum!=want.sum || (fused&&(got.count!=want.count||got.min!=want.min||got.max!=want.max))) std::abort();
                std::vector<double> samples;
                for(unsigned s=0;s<9;++s) {
                    const auto start=std::chrono::steady_clock::now();
                    for(unsigned r=0;r<32;++r) {
                        asm volatile("" : : "r"(values.data()) : "memory");
                        const auto x=run(fused);sink=u64(x.sum)^u64(x.sum>>64)^x.count^x.min^x.max;
                    }
                    samples.push_back(std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/32);
                }
                std::sort(samples.begin(),samples.end());
                std::printf("{\"arm\":\"%s\",\"density\":%u,\"first\":%u,\"op\":\"%s\",\"ns\":%.3f,\"lo\":%llu,\"hi\":%llu}\n",argv[1],density,first,fused?"summary":"sum",samples[4],(unsigned long long)u64(got.sum),(unsigned long long)u64(got.sum>>64));
            }
        }
    }
}
