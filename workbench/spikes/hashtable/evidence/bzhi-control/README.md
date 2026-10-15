# Zen5 BZHI count-definition diagnostic

This compact evidence export contains findings, summaries and codegen proofs. Its `artifact.json` recovers the full package, including all timing rows, disassembly and model inputs. The recorded report recipe regenerates the summaries from that archive using the existing artifact helper. Heavy binaries and source captures remain in the referenced worker archives.

The matched native intervention removes the two anomalous independent-hit losses. This is evidence about the count definition and its consumer dependency, not a hashtable architecture win or intrinsic SeriesPack/TuplePack cost. No live implementation was changed and no patched binary is proposed for production.

Native Zen5 c8a.large, pinned CPU0, Clang21.1.8, x86-64-v3/tune znver5. Twelve balanced rounds cover original, padded partial-count, and full-count binaries; all432 processes execute the complete frozen operations with independent expected results. Values below are median ns per hit (paired speedups are medians of per-round ratios):

| K40 case | Original | MOVB + NOP | MOVL | Paired padded/full |
|---|---:|---:|---:|---:|
| Set A10 Direct |7.032|7.036|2.786|2.525×|
| MM A10 exact |12.782|12.730|5.434|2.338×|
| Set A10 composed control |2.981|2.969|2.986|0.996×|
| MM A10 widened-window control |5.189|5.198|5.160|1.005×|
| Set A6 composed |2.903|2.949|2.210|1.335×|
| Ordinary Abseil set A10 |1.593|1.589|1.569|1.000×|
| Ordinary Abseil MM A10 |4.631|4.670|4.596|1.017×|

All12 paired rounds improve the two main slow cases. The large set uses14336 keys, all in primary buckets; MM uses7168 keys with6771 primary,373 secondary,24 overflow. Padded and original agree closely on both main slow cases. Fast controls and ordinary Abseil do not reproduce the large gain. Serial and full mutation lifecycle results are largely unchanged: for example set Direct serial3.346→3.423ns and MM exact serial13.130→13.136ns; full mutation lifecycle16.463→16.249ns/set key and139.906→141.498ns/MM key. These lifecycle rows include construction, build, update, erase and release, but not read phases. The complete panel, including misses and unfavorable rows, is retained in timing/samples.csv and report/summary.csv.

## What was changed and verified

A frozen source is compiled once. Post-register-allocation assembler changes only immediate40 low-byte definitions at the selected permutation hash.h:26 sites in complete K40 caller/wrapper functions. Padded partial form retains MOVB plus a2/3-byte NOP; full form uses corresponding MOVL. Seventy definitions across set/MM A6/A10 functions are changed, including warm/timed read paths. Other key-generator masks and ordinary baseline functions are unchanged.

Object guards pass310 set and389 MM code sections. Final ELF proof passes1283 identical function addresses/sizes, identical rodata, and entire final text identical after exactly the allowed substitutions. Therefore the matched pair retains call displacements, branches, baseline code and instruction placement. Padding is a separate original-vs-padded control. Padded executes one additional architectural NOP at each changed definition; this is explicitly a residual distinction. The large gains are consistent with the previously modeled carried partial-register dependency, not instruction-count parity or a PMU-derived causal fraction.

The independent100000-input mask oracle covers arbitrary prior upper-register bits. Each mode passes the frozen complete owner checks. Every timed process validates construction, point hit/miss, values, serial graph, updates and erasure. Paired memory, capacity, tier population and operation counts are identical. report/clock-phases.json resolves actual native clock callsites through DWARF; hit extracts include whole-loop wrappers rather than assuming source-order clock ordinals. The unchanged original recompilation reproduces both original prepared-profile anomalies. In this executable the set-baseline and MM-exact timed hit loops are whole-loop format_time wrappers; their root callsites, rather than wrapper-internal line numbers alone, establish the phase. None of the six resolved timed hit regions contains an ordinary call after its starting clock; search/value consumers are fused within the respective loops. The fast composed set still uses a hoisted MOVB-to-R14 count, unchanged at that hit site; the fast MM wide-window count is fully overwritten by an independent constant before its original low-byte update. Not every MOVB40 in a selected function is changed: only the agreed hash.h26 definitions are eligible.

## Provenance and recovery

- Original prepared native source:2013b46d6f86a483ebf11d70ab78e6dffcd5a5b2c702eb57ec3eba8c6756238e, job20261013T194656Z-5a60f63d.
- Guard-passed binary build:job20261013T205609Z-d0bcdc69, source d4f1ba2be4a51d0b4ce0f82aa5fb1e7d98802068e87893d272f4ae81b356d63e. Its archive contains all three ELFs, exact source capture, compiler commands and original/transformed assembler.
- Successful exact-binary timing replay:job20261013T210251Z-71b3ca86, runner source2e2cd9bf632288a4e460e7e1b0010248d8669221ae6382535c10ae6fa4db3563. Binary SHA256s are in timing/binary-input.json and timing/receipt.json. It reruns final byte proof and oracles before measurement; there is no compilation/relinking.
- A replay bookkeeping defect copied the binary-build job.json into raw replay results/job.json. Do not treat that file as the timing-host receipt. provenance/timing-job.json is the actual controller-owned timing job receipt, independently preserved alongside the timing source manifest in its worker archive; timing/host.json and timing/lscpu.json are collected on the actual replay host. timing/binary-input.json explicitly distinguishes the prior binary build. runner/bzhi-recover-run-metadata-fix.py is a corrected future runner, not the source actually timed.
- Earlier attempts204143 and204916 stopped before timing at assembly filename and optional .file checksum-parser assumptions. The205609 build passed native guards but stopped before checks because shutil.copyfile did not retain execute permission on original. The successful replay fixes only file mode. Failure logs and artifact/source references are retained; no failed attempt contributes timing rows.

Use the existing artifact tool with provenance/verified-build-artifact.json to recover full build/source/ELFs, or provenance/timing-artifact.json to recover the complete successful replay. Example from repository root:

    python3 workbench/tools/artifacts.py fetch PACKAGE/provenance/verified-build-artifact.json build/recovered-bzhi-build
    python3 workbench/tools/artifacts.py fetch PACKAGE/provenance/timing-artifact.json build/recovered-bzhi-timing

The package itself retains all selected raw timing rows, rotation, hardware, oracles, complete final disassembly, object/link proof, source recipes, independent model inputs/outputs and failure qualifications. It references immutable full archives instead of duplicating their heavy binaries/source/assembler.

## Scope limits

This fixes an anomalous independent-hit implementation artifact in these frozen consumers. Corrected set Direct remains roughly1.8× slower than ordinary Abseil at A10; MM exact remains roughly1.2× slower on hits and substantially worse over the complete mutation lifecycle. It does not satisfy the broad target or validate the newer live inline-two-value implementation. Only this Zen5 instance/ISA/build is timed; compiler-register allocation changes can create or remove the pattern elsewhere, including other non-native key widths. LLVM-mca inputs under models are models, not measured cycles; profile evidence is independently retained in the owning spike's prepared-profile-zen5 evidence.
