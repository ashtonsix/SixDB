The independent analyzer qualifies 10,752 rows from both x86 hosts: each host passed 16 native and 6 ASan/UBSan always-UAR Loom checks, plus 10 release and 10 sanitized forced controls. Both outer execution orders retain seven repetitions for all eight arms and all six factors. Source 8d640b9e/e1294dfb is identical. See x86-cells.json for every cell and hash.

Large C32, grain4, hinted Ring ns/lookup (forward/reverse):

| Machine | Reference | Pointer tail | Prepared vector | Vector executable manual | Snapshot |
| --- | ---: | ---: | ---: | ---: | ---: |
| Granite AVX512 |15.693/15.807|16.252/16.116|13.880/13.462|13.089/12.791|14.012/13.643|
| Zen5 AVX512 |13.945/13.956|14.255/14.255|12.478/12.554|12.068/12.117|12.457/12.561|

The prepared facts/kernel path closes most of the large C32 gap:3.4–3.6% over its own manual on Zen and5.2–6.0% on Granite. Pointer cold tail alone does not recover the original smaller Cohort result. The sync/tail Frame40B layout adds a tax versus reference 16B; snapshot 48B and vector cold spill costs are included. Fact hoisting/stage kernels change together with native vector representation, so this does not isolate an ABI-only gain.

Hot n128,C32,grain4 stays negative: Zen prepared vector5.142/5.109ns versus own manual4.300/4.195; Granite5.330/5.366 versus5.054/5.120. Grain1 has substantially larger software overhead. Single-stream large results show direction drift; all eight >5% flags are large C1, none C32 or hot. The threshold is descriptive; per-pass ratios precede merged medians.

Normal n128 has128 primary/0 secondary/0 overflow; n4M has4,190,897 primary/3,407 secondary/0 overflow. Thus this campaign times rare secondary cold work (0.0812%), not a chain-heavy trace. Actual forced cases cover32 secondary/80 overflow, ragged partial cohorts, budgets and cancellation; Ikea's separately qualified full hierarchy supplies the aged, genuinely chained native workload.

Independent direct assembly parsing confirms both x86 hosts: pointer-tail g4 ready payload has zero ordinary calls and24 negative RSP memory instructions; prepared vector/snapshot g4 likewise has no ordinary calls but36 negative RSP memory instructions. Their metadata shrinks 252 to 181 instructions and loses three stack-reference instructions; payload expands 195 to 366. Whole-operation throughput improves despite that static payload shape. This does not prove spill traffic is lower on every path.

Cold accounting includes the complete callback/helper: pointer-tail g4 explicitly allocates 56B; prepared vector g4 allocates 168B; snapshot g4 wrapper 8B plus ordinary finish_cold 152B. Those are only these functions' explicit stack frames, not nested helpers, red-zone total usage or dynamic counters. Static memory/call counts cover all branch paths and cannot diagnose IPC/stalls.

No main source or worker was changed/launched by this interpretation. The V2 same-source 4GiB retry remains pending.
