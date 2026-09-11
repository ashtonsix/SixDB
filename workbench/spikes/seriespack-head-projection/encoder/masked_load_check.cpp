// Isolate inactive-lane fault suppression from the head encoder and compiler
// vectorizer. The last readable dword is followed immediately by PROT_NONE.
#include <immintrin.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

extern "C" [[gnu::noinline]] __m256i masked_dwords(const int* p, __m256i mask) {
    return _mm256_maskload_epi32(p, mask);
}
int main() {
    const auto size = sysconf(_SC_PAGESIZE);
    if (size <= 0) std::abort();
    auto* mapping = static_cast<char*>(mmap(nullptr, size * 2, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (mapping == MAP_FAILED || mprotect(mapping + size, size, PROT_NONE) != 0) std::abort();
    auto* source = reinterpret_cast<int*>(mapping + size - sizeof(int));
    *source = 129;
    const auto value = masked_dwords(source, _mm256_setr_epi32(-1,0,0,0,0,0,0,0));
    std::array<int,8> actual{};
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(actual.data()), value);
    if (actual[0] != 129) std::abort();
    for (unsigned i = 1; i < actual.size(); ++i) if (actual[i] != 0) std::abort();
    if (munmap(mapping, size * 2) != 0) std::abort();
    std::puts("masked dword guard: one active readable lane, seven inactive inaccessible lanes passed");
}
