#include "tile-bench.h"
#define W(K,N,O,A) \
extern "C" [[gnu::noinline]] std::uint64_t tile_##K##_##N##_##O##_##A(const std::uint8_t* p, std::uint64_t q, std::uint64_t active) { \
    return tiles::call<K,N,ikea::comparison(O),A>(p,q,active); \
}
#define ARMS(K,N,O) W(K,N,O,0) W(K,N,O,1) W(K,N,O,2) W(K,N,O,3)
#define OPS(K,N) ARMS(K,N,0) ARMS(K,N,1)
OPS(7,8)
OPS(12,8)
OPS(16,8)
OPS(24,8)
OPS(30,8)
OPS(32,8)
OPS(40,8)
OPS(64,8)
OPS(12,24)
OPS(16,24)
OPS(30,16)
OPS(32,16)
OPS(12,40)
