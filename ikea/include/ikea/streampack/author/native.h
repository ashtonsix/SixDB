#pragma once
#include <ikea/streampack/author/read.h>
#include <ikea/boundlist/author/native.h>
namespace ikea::streampack::native {
using namespace ikea::boundlist::native;
#if defined(__aarch64__) || defined(__AVX2__)
using boundlist::sidecar::native::chunks;
using boundlist::sidecar::native::each_chunk;
using boundlist::sidecar::native::group;
using boundlist::sidecar::native::packet;
using boundlist::sidecar::native::read16;
#endif
} // namespace ikea::streampack::native
