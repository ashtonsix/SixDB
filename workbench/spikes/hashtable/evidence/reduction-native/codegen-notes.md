# What changed in the native read loops

All source and binary identities are in the adjacent platform contexts. These
observations inspect actual timed loops resolved through source/DWARF callsites;
they do not assign cycle costs to individual instructions.

For the Zen5 K40/A10 composed map serial loop, borrowed helper `0x9345b0`
updates the checksum in memory (`addq %rcx,(%rbx)` at `0x9345ec`). The local-result
helper `0x950d60` accumulates in R15. Owner/seed/base loads remain in its per-key
lookup loop. Timing is 11.001 → 10.951 ns/op, so removing those checksum writes
alone does not deliver the large hoped-for serial improvement.

The V2 equivalent changes from borrowed helper `0x8403a0`, with a checksum
load/add/store at `0x840434`–`0x84043c`, to local helper `0x860e00` with checksum
held in X20. Owner and seed loads still occur at the per-key loop header
(`0x860e94` onward). Serial timing is 17.286 → 17.245 ns/op. This establishes
that the intended accumulator change reached native machine code; it does not
establish which remaining dependency dominates latency.

The Zen5 K40/A10 ordinary Abseil miss changes much more: 5.032 → 1.833 ns/op.
The borrowed baseline already held its checksum and table metadata in registers.
The borrowed timed region starting `0x9c8a04` tests table shape per outer round
and rematerializes constants inside its inner loop; local-result whole-loop
helper `0x9ec830` hoists shape/constants and changes loop/pointer structure.
Consequently this gain must not be labeled the cost of a borrowed checksum
store, nor assigned solely to one hoisted branch. Process seed/placement also
remain uncontrolled. Comparable candidate miss is 2.745 → 2.804 ns/op.

Across all audited reads, candidate x86 loops are whole-loop helpers (270 per
binary); V2 has 264 helpers and six timer-inlined loops. Borrowed baselines have
270 timer-inlined read loops per platform. Local baselines have 238 inline plus
32 whole-loop helpers on each x86 host, and 239 plus 31 on V2. No warmed loop
contains a per-key lookup or per-value callback call. This means whole-loop
outlining is not itself a lost fusion boundary; its effect on register allocation,
hoisting and placement must be distinguished from per-operation dispatch.

Selected complete K40/A10 map serial helpers and Zen5 Abseil miss regions/helpers
are kept beside the respective platform evidence. Complete assembly and ELFs
are recovered from the worker artifact; selected witnesses do not replace the
all-case call-boundary audit. There is no PMU or causal hardware analysis here.
