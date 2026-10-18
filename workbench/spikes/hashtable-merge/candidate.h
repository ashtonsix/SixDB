#pragma once
#include "merge.h"
namespace merge_study {
template <class D, class S, class Reduce>
merge_status accumulate_candidate(D& dst, const S& src, Reduce&& reduce) {
    return research_status(ikea::hashtable::accumulate(dst, src, std::forward<Reduce>(reduce)));
}
// Checker access to maintained physical codecs; no separate implementation.
using namespace ikea::hashtable::detail::accumulation;
} // namespace merge_study
