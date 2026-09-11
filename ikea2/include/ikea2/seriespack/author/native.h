#pragma once
#include <ikea2/seriespack/detail/point.h>
#if defined(__aarch64__)
#include <ikea2/seriespack/detail/native/neon/read.h>
#include <ikea2/seriespack/detail/native/neon/groups.h>
namespace ikea2::seriespack {
namespace native = neon;
namespace native_group = neon_group;
} // namespace ikea2::seriespack
#elif defined(__AVX2__)
#include <ikea2/seriespack/detail/native/avx2/read.h>
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
#include <ikea2/seriespack/detail/native/avx512/read.h>
namespace ikea2::seriespack {
namespace native_group = zmm;
}
#endif
namespace ikea2::seriespack {
namespace native = x86;
}
#endif
