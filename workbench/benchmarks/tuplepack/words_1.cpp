#include "word_cases.h"
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
const bool registered = [] {
  word_methods<1>();
  return true;
}();
} // namespace
#endif
