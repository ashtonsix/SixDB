# Selected codegen diagnostic

`prefix.cpp` includes the production u16 inverse body. `prefix-neon.s` is Clang
21.1.8 output with the final header, `-O3 -fno-vectorize -fno-slp-vectorize
-fno-asynchronous-unwind-tables -march=armv8-a+simd -mtune=neoverse-v2`.
Compile on Linux with `-std=c++23 -Iikea/include -S`.

The emitted path has two vector loads, explicit prefix/carry operations, vector
comparisons and one horizontal reduction. No per-chunk branch or scalar carry
round trip survives. The compiler folds the mask sum into an AND/subtract/addv;
this preserves the unsigned <= count. Correctness tests include zero/max widths.

Run the shared `workbench/tools/mca.py` on that assembly with
`--triple=aarch64-linux-gnu --cpu=neoverse-v2 --mattr=+neon`. LLVM21.1.8 reports
25 instructions, 29 uops and **5 cycles block reciprocal throughput**. This is a
backend model, not measured request latency or throughput. Its repeated total
(3303 synthetic cycles/100) is particularly unsuitable as independent-call
latency: returning w0 feeds the next modeled load through x0. Returns do not
transfer the modeled PC. See the shared [model limits](../../../../../tools/mca.md).

The complete consumer assembly is retained with each native worker bundle, not
copied into Git. The first cursor replay showed real stack traffic around
memmove: its mapped-effects member was borrowed by the geometry member, forcing
self-referential state into memory. Passing the transient mapping into geometry
append removes that cycle; the final write replay compares the resulting complete
construction/packing/transfer with frozen callers and independent fused writers.
No scheduling model substitutes for those live comparisons.
