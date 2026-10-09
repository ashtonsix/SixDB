#pragma once
#include "bodies.h"

namespace orbital_versioned_performance {
// Named complete callers used by the executable and inspected in its objects.
// These are invoked once per trial, not invented out-of-line per-row seams.
Interval run_orbital_reads(Orbital&,bool borrowed,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch,std::uint64_t& bindings);
Interval run_lmdb_reads(Lmdb&,const Corpus&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch,std::uint64_t& bindings);
Interval run_direct_reads(const Corpus&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch);
Interval run_row_lmdb_reads(Lmdb&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<orbital_bench::Outcome>,std::size_t batch);
Interval run_orbital_codec_reads(Orbital&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch,std::uint64_t& bindings);
Interval run_lmdb_codec_reads(Lmdb&,const Corpus&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch,std::uint64_t& bindings);
Interval run_orbital_consume_reads(Orbital&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch,std::uint64_t& bindings);
Interval run_lmdb_consume_reads(Lmdb&,const Corpus&,bool latency,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,std::size_t batch,std::uint64_t& bindings);
} // namespace orbital_versioned_performance
