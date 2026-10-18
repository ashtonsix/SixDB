# What the checks establish

`orbital/test/live_tail.cpp` tests the common allocation contract, not a public
Window API. It uses byte patterns and real mapping/residency queries alongside
accounting: useful bytes, historical readable padding, duplicate/moved/foreign
Holds, unchanged addresses and VA, page-granular refunds and later regrowth.

Paused helper calls distinguish owner submission, syscall completion and owner
reply. Allocation release, Representation capture, close and implicit final-Arena
teardown exercise those boundaries. Wrapped discard/protection calls fail both
before and after partial work; retries must settle the same charged range once.
First-Hold tracking refusal, non-power-of-two endpoint bins, continuing prefix
readers and finite allocations sharing a native page cover the representation
choices that simple shrink-and-read tests miss.

The tests run against the host's native page size. `mincore` checks touched
anonymous backing; it is not a process-RSS or throughput measurement. Injected
syscall failures exercise conservative recovery, not every kernel failure mode.
Actual foreign releases provide real concurrency but do not enumerate all
interleavings. Native memory/throughput comparisons belong to the study and its
consumer evidence, not these correctness assertions.
