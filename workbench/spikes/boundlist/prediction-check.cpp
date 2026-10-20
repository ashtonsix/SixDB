#include "prediction.h"
#include <algorithm>
#include <cassert>
#include <vector>
int main() {
    for(unsigned n: {0u,1u,7u,31u,32u,33u,64u,256u,4096u})for(unsigned shape=0;shape<4;++shape) {
        std::vector<std::uint64_t> data(n);
        for(unsigned r=0;r<n;++r) {
            // Extreme dynamic range, duplicates, and a jump hidden between the
            // source-preparation quantile samples. Prediction is never proof.
            data[r]=shape==0?std::uint64_t(__uint128_t(r)*UINT64_MAX/std::max(1u,n)):shape==1?r/17:
                shape==2?(r<n/2?0:UINT64_MAX):(r<n*5/16?r:r+1000000);
        }
        const auto model=prediction_spike::model<std::uint64_t>::prepare(data.data(),n);
        for(auto q:{0ull,1ull,17ull,128ull,1000000ull,0x8000000000000000ull,0xfffffffffffffffeull,0xffffffffffffffffull}) {
            const unsigned expected=std::lower_bound(data.begin(),data.end(),q)-data.begin();
            assert(prediction_spike::search<false>(data.data(),n,std::uint64_t(q),model)==expected);
            assert(prediction_spike::search<true>(data.data(),n,std::uint64_t(q),model)==expected);
            assert((prediction_spike::search<true,true>(data.data(),n,std::uint64_t(q),model)==expected));
        }
        for(auto q:data) {
            const unsigned expected=std::lower_bound(data.begin(),data.end(),q)-data.begin();
            assert(prediction_spike::search<false>(data.data(),n,q,model)==expected);
            assert(prediction_spike::search<true>(data.data(),n,q,model)==expected);
            assert((prediction_spike::search<true,true>(data.data(),n,q,model)==expected));
        }
    }
}
