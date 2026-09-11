# V2 checked-get candidate: changed call, unchanged bound control

This is an independent inspection of the actual paired worker binaries from job `20260911T062438Z-2d2cfcb9`, compiled with Clang 21.1.8, `-O3 -march=armv8-a -mtune=neoverse-v2`. It supplements the hardware pair; it is not another timing run. The source alternative and current baseline remain separate from production changes.

Only checked `get` changes among 1,244 common operations-object functions when comparing disassembled instructions and relocation references. No function is added or removed. Checked get falls from 144 to 96 instruction bytes, removes the copied view and full reader construction, and reduces its stack frame from 256 to 32 bytes. The same input range check remains. The caller still pays its existing checked-call argument/result handling.

The six checked-call witnesses improve in both ordering directions. The unchanged bound Striped5 control instead rises from 2.392 to 3.214 ns/query, with candidate/base ordering ratios 1.353 and 1.313. This control must stay visible; its timing is not subtracted from checked calls.

The 27 instructions between the bound caller's benchmark start/finish calls are byte-identical at the same address, `0x91580`. They include its actual repeated query loop and indirect call. The selected arithmetic Striped5 point function is also byte-identical (116 bytes, 29 instructions), with no calls, stack accesses or table references. It contains one data-dependent branch. Its linked address moves from `0xb0ee0` to `0xb0ea0`: the same offset within a 64-byte line, a different offset within 128 bytes. The entire benchmark `.rodata` contents are identical and move by the same 64 bytes.

These observations rule out a changed point instruction algorithm and changed timed caller instructions as explanations for this control movement. Linked placement and execution context remain hypotheses; no cache, branch-predictor or store-forwarding cause is established. A controlled placement test could distinguish part of that uncertainty, but this inspection does not supply it. There is no basis here for a format-specific production exception.

`review.json` records object/binary identities, exact hot-loop and point bytes/hashes, function counts, addresses and rodata hashes. Selected get, point and timed-loop assembly is adjacent. Full paired binaries and archives are recoverable through the owning worker bundle, rather than duplicated with this compact review.
