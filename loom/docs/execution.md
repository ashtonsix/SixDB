# Direct and Ring execution policy

[`loom::execution::Policy`](../include/loom/execution.h) selects between legal
complete direct and memory-Ring executions at an ordinary operation boundary.
It uses completed serial consumer samples to predict whether direct execution
is cheap for one caller cohort. It does not measure cache residence, grant read
rights or decide whether an operation may suspend. Tasks remain the caller's
path for work that suspends.

For a prepared owner with separate hot and cold entries,
[`ColdPromotion`](../include/loom/execution.h) confirms useful direct-batch
feedback in the cold method. The [cold-promotion pattern](#cold-promotion-without-hot-path-observation)
keeps observation out of the hot method; the caller owns demotion and entry lifetimes.

The caller owns the Policy on its assigned thread and defines the evidence
cohort: the work for which samples apply to the chosen complete procedures.
It calls `reset()` when that qualification changes. Loom defines no cohort
identifier or container identity schema. The Policy owns no input, output,
frame, allocation or binding. Preserve their lifetime, content stability and
permission through both complete
executions. A synchronous Ring drain can use the ordinary owner borrow under
the [allocation rules](../README.md#allocation-stage); escaping work retains its
own protection.

## Choose at an operation boundary

| Caller facts | Fast usage pattern |
| --- | --- |
| One legal path, or the caller already knows which complete path to run | Execute that path without policy selection or observation. |
| Both paths are legal, but the operation has too little useful independence | Use direct execution. A wide bank cannot create independent requests. |
| Both paths are legal and a qualified caller fact already favors direct execution | Execute direct, or pass `Prior::direct` when maintaining Policy context. The prior requests no sample. |
| A prepared owner has hot/cold entries and an explicit owner maintenance boundary | Use `ColdPromotion` in the cold method; install hot on confirmation and demote through caller maintenance. |
| Both paths are legal, useful independence exists, and locality varies | Keep one owner-local Policy for the qualified cohort and select once for the operation. |

A prior is a caller decision about the qualified complete operation. Repeated
addresses, a hardware profile or an issued prefetch alone do not establish cheap
execution. Calling the Policy with a prior still checks sample age and epoch
ordering; bypass it when that bookkeeping serves no purpose.

Keep cheap neighboring work fused in the direct path. Entering CPS or Ring adds
a boundary that the complete operation must repay. The Policy chooses between
caller implementations; it does not prepare kernels or insert stages.

## Bind stable state before the loop

`Policy::bind(Options)` checks the cutoff, maximum sample age, minimum consecutive
cheap samples, minimum useful independence and sample interval. Zero values are
refused. There is no default cutoff or age. Options stay immutable; replace the
Policy to change them. `reset()` forgets evidence and cadence while retaining
the options.

Prepare the direct and Ring implementations over the same useful operation and
result. Bind formats and demands, allocate stable frames, and configure the bank
outside the steady-state loop when their lifetime permits reuse. A batch choice
does not rebind formats, resize the bank or allocate per-request policy state.
The [compiled example](../examples/execution.cpp) keeps its checked demand,
frames, Ring and Policy across batches, then closes the Ring before releasing
its input and shutting down the Worker.

Keep one Policy for each independently learned cohort, or reset it before using
it for a different qualified operation. Any cohort lookup and semantic
invalidation belong to the caller, outside native visits. Loom neither checks
those assumptions nor reconstructs them from an address or tag. A sample from
one subset is not automatically evidence about the caller's wider workload.
When changed procedures, access patterns or execution conditions invalidate the
comparison, reset its evidence and requalify the cutoff as needed. Reset does
not change the immutable options.

`Epoch` is a full caller-supplied monotonic counter in a consistent unit, with
`max_age` in that unit. It measures elapsed time across idle intervals, rather
than just counting operations. The age limit is inclusive. Selection and
sampling boundaries discard evidence on expiry or a backward epoch; returning
later to an old time cannot resurrect it. Epoch wrap is a backward transition.
The caller supplies time at the operation boundary; the Policy reads no clock
or profile atomic.

`Paths` describes legal complete paths, useful independent requests and the
actual configured Ring width. Legality takes precedence. With both paths legal,
insufficient useful independence chooses direct. A caller-known direct `Prior`
also chooses direct without manufacturing serial evidence. Otherwise unknown
or expensive serial work chooses Ring, and qualified cheap samples choose direct.
Count requests that can make independent useful progress, not dependent steps
within one request. The width is the actual bank configuration, not a hardware
miss-resource count.

## Keep observation outside native visits

### Sampling sequence

Call `sample_due(now, paths, prior)` once per operation, optionally execute a
bounded serial probe, then select the useful operation's path. If a probe
completed, call `observe(completed_at, sample)` with its completion epoch and
`SerialSample`, then call `select(completed_at, paths, prior)`. Without a probe,
call `select(now, paths, prior)`. Execute the selected complete path for the
batch. The compiled example demonstrates this sequence, including independent
checks of both fixed paths before adaptation. Keep clocks, sample decisions and
policy selection at the ordinary batch boundary. None belongs inside each
bucket, item or native continuation visit's few-nanosecond budget.

`select` does not consume cadence. Calls with only one usable path, insufficient
independence or a known-direct prior request no sample and consume no cadence.
They still discard expired evidence or evidence preceding a backward epoch.
Semantic invalidation requires the caller's explicit `reset()`.

The caller owns the sample's grain, representativeness and subset rotation.
Complete each sampled request's dependent work before the next. Include all
result-dependent payload work; neither hint issuance nor pipelined reciprocal
throughput is a `SerialSample`. This measures software-serial complete-consumer
cost with normal CPU overlap, rather than one-outstanding-miss or isolated load
latency.
Report its actual elapsed nanoseconds and completed request count
through `observe`, using the completion epoch. Zero work/time is rejected
without changing history. Cost is rounded upward safely; any fraction above the
cutoff is expensive. An expensive sample breaks the cheap confirmation sequence.
Charge the entire probe, its clocks and policy work to the operation; repeated
sample queries do not increase useful request counts.

An initial or incomplete cheap record requests another sample at the next
eligible boundary. The first expensive observation gets one early retry; a
second expensive observation backs off for `sample_interval` eligible calls.
Confirmed cheap evidence uses the same interval. If the interval is 32, calls
one through 31 after the observation are not due and call 32 is due. A skipped
or rejected requested sample remains due. A saturating countdown avoids a
wrapping operation counter. Explicit reset, expiry and epoch rollback reset the
cadence.

This bounds retry cadence while evidence remains fresh. It is not a time or
work-rate limit: if evidence expires before every operation, every operation can
request a probe. A caller with no observation budget can skip it; without fresh
cheap evidence, selection stays on Ring when useful and legal. Small cheap
samples can miss heterogeneous costs, and still-fresh evidence can select direct
after unreported interference. The policy supplies no latency guarantee.

## Cold promotion without hot-path observation

`ColdPromotion` is 16 bytes of owner-local confirmation state. It owns no method
pointer, timer, expiry record or read protection. A caller prepares its legal
direct/Ring procedures and stable state, then uses `ColdPromotion::bind` with an
explicit `batch_requests` and consumer-qualified `cheap_ns_per_request`.
`cheap_confirmations` defaults to two; zero options or more than 65,535
confirmations return `invalid_argument`. Options remain immutable.

The cold method may execute and time a **useful direct batch**, then pass
`DirectBatchSample{elapsed_ns, completed_requests}` to `observe`. Independent
requests may interleave within that batch; this is distinct from the dependent
software-serial work accepted by `Policy::observe(Epoch, SerialSample)`.
Consume the same results and payloads as the complete operation and execute each
useful request once. Charge the clocks, confirmation and any recurring binding
to the operation. A fast Ring batch can reflect successful latency hiding, so
its time cannot substitute for direct-batch evidence. The caller chooses a cold
sampling cadence; this helper introduces none.

The useful count must equal the bound grain. Zero elapsed time or a wrong count
leaves state unchanged. A valid sample is cheap at or below the whole-batch
cutoff; any fraction above it breaks the consecutive cheap sequence. `observe`
returns true once when the required confirmations arrive. False means no new
promotion, including for invalid or expensive samples. After true, the state
stays latched until `reset`; observing it again requests no second registration.

In the separate hot/cold entry pattern, the caller uses that confirmation to
install its prepared hot procedure at a returned boundary. The hot procedure
runs without calling this helper, reading a clock or updating tracking stamps.
The caller decides how long that choice remains qualified and restores cold
before resetting confirmation when its assumptions change.

A caller using expiry-based demotion owns the registration and maintenance
path. If required registration cannot complete, keep or restore cold and reset
confirmation. Retire any pending maintenance before destroying its state, or
retain the state until that use ends. `ColdPromotion` schedules no timer or
Worker work. Replace the confirmation state when its grain or cutoff changes,
and keep the operation's lifetime and access guarantees valid independently
through both procedures and any queued work.

The method names describe execution paths, not cache residence. A coarse sweep
can leave hot selected until its next service boundary; this helper establishes
no freshness or latency bound. Qualify the cutoff, sampling, demotion cadence
and total transition/maintenance cost for the actual consumer. Loom supplies no
tuned performance preset. The [independent checks](../test/cold_promotion.cpp)
cover the confirmation and reset contract.

## Qualify the complete consumer

Compare both complete executions over the same format, distribution, useful
results, payload work, batch size and actual Ring width. Include admission and
result handling that recur per operation; keep reusable setup outside both
timers. An ordinary direct loop may itself interleave work. Low serial cost alone
does not establish which complete implementation has better throughput.

Choose the cutoff from that comparison, and requalify when the compared kernels,
access distribution or execution configuration change the crossover. Larger
batches can amortize boundary costs and expose independence, but their buffering
and completion delay belong to the consumer's tradeoff. A sample's bounded grain
and rotation must also represent that consumer. The example's explicit cutoff
is illustrative, not a preset.

Keep the measurements distinct:

- A serial sample times completed dependent consumer work, with normal CPU
  overlap. It predicts serial cost for this cohort; it is neither isolated load
  latency nor a complete operation's latency bound.
- Reciprocal throughput is elapsed time per completed useful request with
  independent work in flight. It qualifies the direct/Ring choice, but must not
  enter `SerialSample`.
- Cache residence requires separate evidence. Neither a cheap sample, repeated
  access nor a hint proves a cache service tier.

## Historical experiment evidence

The [locality comparison](../../workbench/spikes/loom-locality-execution/GRADUATION.md)
retains charged direct/Ring controls, gains and a higher-cutoff counterexample
that chooses direct too often. It measured an earlier identity-bearing Policy;
its layout, wrapper costs and timings do not qualify the current API or the
caller's reset adapter. The [cold-feedback study](../../workbench/notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
retains complete ownership, expiry and tracking costs alongside mixed-trace
gains and cold-only, large-reused and transition losses. The [compact-heat calibration](../../workbench/notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
retains a rejected predictor that regresses on its bounded hashtable batches.
These experiments qualify their particular callers and earlier interfaces;
they supply neither a Loom container protocol nor a universal policy preset.
Memory hint lowering remains separate from execution selection.
