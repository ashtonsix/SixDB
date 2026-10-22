#include <vector>
#include <array>
#include <random>
#include <cstdint>
#include <cstdio>
#include <algorithm>
using u64=std::uint64_t;
namespace fe { constexpr unsigned block_rows=256; }
constexpr unsigned requests=8192;
inline std::size_t next(std::size_t state, u64 value, std::size_t mask) {
    return (state * 713 + (value ^ (value >> 32)) + 101) & mask;
}
void trace(unsigned n,unsigned bits,unsigned blocks,unsigned chains) {
    std::mt19937_64 random(713);
    std::vector<u64> values(std::size_t(blocks) * fe::block_rows);
    const auto mask = bits == 64 ? ~u64{} : (u64{1} << bits) - 1;
    for (unsigned b = 0; b < blocks; ++b) {
        if (!n) {
            for (unsigned row = 0; row < 256; ++row) values[std::size_t(b) * 256 + row] = random() & mask;
            continue;
        }
        std::vector<u64> runs(n);
        for (unsigned r = 0; r < n; ++r) {
            do { runs[r] = random() & mask; } while (r && runs[r] == runs[r - 1]);
        }
        for (unsigned row = 0; row < 256; ++row)
            values[std::size_t(b) * 256 + row] = runs[std::min(n - 1, row * n / 256)];
    }

    std::vector<std::size_t> states(chains);
    for(unsigned c=0;c<chains;++c)states[c]=c*137&(values.size()-1);
    std::vector<bool> visited(values.size()); unsigned unique=0;u64 sum=0;
    for(unsigned q=0;q<requests/chains;++q) for(unsigned c=0;c<chains;++c) {
        auto r=states[c];unique+=!visited[r];visited[r]=true;
        auto v=values[r];sum+=v;states[c]=next(r,v,values.size()-1);
    }
    for(auto s:states)sum^=s;
    std::printf("{\"runs\":%u,\"bits\":%u,\"blocks\":%u,\"chains\":%u,\"unique_rows\":%u,\"checksum\":\"%llu\"}\n",n,bits,blocks,chains,unique,(unsigned long long)sum);
}
int main(){for(unsigned b:{1u,256u})for(unsigned w:{8u,32u,64u})for(unsigned n:{0u,4u,8u,15u,16u,17u,32u,64u,128u})for(unsigned c:{1u,8u})trace(n,w,b,c);}
