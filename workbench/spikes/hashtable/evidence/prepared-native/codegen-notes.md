# Stable Zen5 anomalies and a focused profiling question

The retained Zen5 executable SHA is
`9907b313adb693150c7717f758150bc2b981b1a566a672eeb588d59743f0a6ac`.
All addresses below are ELF-relative; this is a PIE binary, so sampling must
normalize its runtime mmap before assigning samples to phases. Complete timed
CFGs are retained beside the Zen5 measurements. No timing region below contains
a call. Full read consumers are fused, including every multimap value.

| K40/A10 hit arm | Median ns/op | Nine-rep range | Actual hit PC interval |
| --- | ---: | ---: | --- |
| Set exact composed | 2.895 | 2.871–2.961 | `[0x800334,0x80073e)` |
| Set exact Direct | 6.998 | 6.955–7.036 | `[0x813583,0x81399b)` |
| Multimap exact composed | 12.737 | 12.578–12.840 | `[0x957436,0x9578f5)` |
| Multimap exact Direct | 8.373 | 8.323–8.465 | `[0x965f0a,0x966403)` |
| Multimap wider-both composed | 5.039 | 4.968–5.214 | `[0x9f00f3,0x9f05bb)` |

The differences are stable within this process's repetitions; that does not
remove placement, seed, branch-predictor or codegen confounding. Exact versus
wider-both intentionally changes admitted loads while retaining layout, whereas
composed versus Direct changes field-code implementation. Neither pair is an
identical-machine-code control.

There is a concrete next-key dependency hypothesis. Slow set Direct writes
`movb $0x28,%cl` at `0x81361c`, then uses full RCX as the `bzhiq` count operand at
`0x813628`. On the common successful primary path, RCX was produced by the
previous query's suffix-match index. Slow multimap exact similarly writes CL at
`0x9574c7` and uses RCX at `0x9574dd`; its previous loop iteration copies the
accumulated result to RCX at `0x9574b2`. A partial-register definition may leave
an artificial dependency from the prior lookup to the next key mask.

Fast set composed initializes its count in R14 once before the loop at
`0x800364`. Fast multimap wider-both fully defines RAX with a constant at
`0x9f01dc`, then sets AL at `0x9f01f4` before BZHI at `0x9f01fc`; that particular
prior-query dependency is absent. This is an architectural dependency hypothesis,
not proof that hardware waits on it or that it explains the measured gap.
Both pairs also change allocation of registers, loop shape, branch directions
and exact tail reconstruction. Static instruction counts cannot settle cause.

Minimal original-binary profiling uses these four case/arm commands:

```
formats run 40 0 0 REPS 1
formats run 40 0 1 REPS 1
formats run 40 2 0 REPS 1
formats run 40 2 6 REPS 1
```

The intermediate multimap Direct arm is `formats run 40 2 1 REPS 1`. Around
1,000–2,000 complete repetitions should provide timed-hit samples without a new
driver; actual required duration depends on PMU availability and sampling rate.
Keep original full checks and all phase CSV output. Use the existing PMU
availability probe and normalize perf mmap/IPs. Attribute samples only to audited
hit PCs; whole-process counters include build, warmup, mutation and destruction.

The question is whether slow-arm samples concentrate at the next-key BZHI and
primary dependent-load chain, or instead at branch recovery/front-end/bag paths.
Set Direct's primary chain spans `0x81360f`–`0x8136bc`; multimap exact's spans
`0x9574c4`–`0x95758e`, with bag resolution/consumption at
`0x957752`–`0x95784c`. Fast multimap's corresponding primary region is
`0x9f01d8`–`0x9f0294` and bag region `0x9f0452`–`0x9f054c`.
Samples locate stalls only approximately and do not prove a dependency.

A decisive later control would fully define the BZHI count register, retain
exact fields/layout and the same complete operation, and re-audit the generated
loop. If the suspected count chain disappears without narrowing the timing gap,
that rejects it as the principal explanation. No such modified-code benchmark
or PMU result is included here; the parent task owns that follow-up.
