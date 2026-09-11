# Encoder ABI code and build review

Read-only inspection of retained job `20260911T013319Z-45302832`. The script
verifies all link-input hashes and archive copies before extracting objects.
Source-hash comparison finds exactly the intended three files, +10/−9 lines.
There are no new physical variants, private headers or dispatch policies.

| Object | Before text bytes | After text bytes | Delta |
| --- | ---: | ---: | ---: |
| Native | 3,051,595 | 3,050,091 | −1,504 |
| Scalar/operations | 1,393,835 | 1,392,638 | −1,197 |
| View | 3,435 | 3,435 | 0 |
| Actual bulk benchmark | 43,886 | 43,963 | +77 |
| Actual casing benchmark | 95,549 | 95,405 | −144 |

All native shrinkage is in 154 `encode_bound` bodies (59,342→57,838 bytes).
All scalar shrinkage is in 77 `encode_bound_scalar` bodies (66,161→64,964).
Native and scalar read-only/data section sizes are unchanged. Other family
counts and sizes are in `summary.json`; symbol-byte sums equal text-section
bytes for these two objects. Object file sizes include debug information and
must not be presented as executable code size.

There are 3,404 native, 1,174 scalar and two view common symbols with unchanged
disassembled instruction bodies. All added/removed names in the first two
objects are the callbacks whose C++ signatures changed. This comparison
excludes relocation targets, constant contents and final linked placement.
It is not proof that the complete linked non-encoder program is identical.

## Actual caller inspection

`timed-caller-excerpts.txt` retains the inspected instruction ranges; full
annotated object disassembly is adjacent. These are actual compiled benchmark
callers, not the small illustrative caller witness from preparation.

* Bulk: before 0x2500–0x2558 / after 0x2540–0x2564 (through the indirect call).
  Carrier-width dispatch remains. The after path eliminates the source-count
  load, construction of a 24-byte temporary, and the second 24-byte outgoing
  argument copy. Source pointer and width are passed in registers.
* Bound casing, no effects: the per-iteration argument setup through the
  indirect call shrinks from eight instructions to five. The outgoing 24-byte
  aggregate and vector stack copy disappear. Iteration checks remain.
* Bound casing, with effects: nine instructions become six, including the
  effect-size reset in both. Effect reporting itself remains in the callee.
* Checked casing: both whole caller instruction bodies and sizes are unchanged
  (3,297 bytes without effects, 3,342 with them). Checked admission retains the
  public capacity-bearing input contract; the trusted call beneath it changes.

Those counts describe instructions in specific retained paths, not cycle
costs. Whole bulk function size grows despite this locally simpler call; it
contains setup and several operations with compiler register/layout changes.
The casing object also changes its bind-encoder path, which performs an
untimed bound encode for validation.

## Observed compilation costs

The last successful edge in each retained `.ninja_log` gives the following
wall durations. Both records have one successful edge per listed TU.

| TU | Before seconds | After seconds |
| --- | ---: | ---: |
| native.cpp | 64.797 | 63.924 |
| operations.cpp | 30.336 | 29.785 |
| view.cpp | 0.233 | 0.230 |
| seriespack_bench.cpp | 1.508 | 1.470 |
| seriespack_casing_bench.cpp | 2.199 | 2.179 |

`compile-edges.csv` includes all 66 before/after edges, including checks and
other benchmark TUs. These jobs have different cache states and launch times;
these are observed costs, not a controlled compile-time improvement. No
runtime conclusion is drawn here. Timing and source/sample audits are owned
by the physical implementation task and must be considered separately.
