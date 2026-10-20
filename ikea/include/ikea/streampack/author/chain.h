#pragma once
#include <ikea/streampack/author/native.h>
#include <ikea/boundlist/author/chain.h>
namespace ikea::streampack {
#if defined(__aarch64__) || defined(__AVX2__)
using boundlist::sidecar::chain;
using boundlist::sidecar::pipeline_location;
using boundlist::sidecar::point_chain;
#endif
} // namespace ikea::streampack
