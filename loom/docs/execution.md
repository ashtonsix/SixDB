# Direct and Ring execution policy

[`loom::execution::Policy`](../include/loom/execution.h) selects between legal
complete direct and memory-Ring executions at an ordinary operation boundary.
It uses completed serial consumer samples to predict whether direct execution
is cheap for one caller cohort. It does not measure cache residence, grant read
rights or decide whether an operation may suspend. Tasks remain the caller's
path for work that suspends.

The caller owns the cohort and Policy on its assigned thread. No Worker field,
shared address directory or per-bucket observer is involved. The Policy owns no
input, output, frame, allocation or binding. Preserve their physical lifetime,
content/version stability and permission independently through both complete
executions. A synchronous Ring drain can use the ordinary owner borrow under
the [allocation rules](../README.md#allocation-stage); escaping work retains its
own protection.

## Binding a consumer

`Policy::bind(Options)` checks the cutoff, maximum sample age, minimum consecutive
cheap samples, minimum useful independence and sample interval. Zero values are
refused. There is no default cutoff or age. Qualify the cutoff against both
complete executions over the same format and useful work: low serial cost alone
does not establish the throughput winner of an ordinary interleaved loop.
Options stay immutable; replace the Policy to change them. `reset()` forgets
evidence and cadence while retaining the options.

Each boundary supplies an `Identity` containing owner incarnation, storage or
version generation, population, assigned worker and cache domain. Owner and
population must be nonzero; worker/domain must differ from `unknown_domain`.
Generation zero is valid. The caller defines these names. Population includes
the complete compared direct/Ring procedure pair and access distribution. A sample
from one selected subset is not automatically evidence about the whole table.
Use separate policies or change identity when comparing other legal kernel
variants, such as whole-bucket and located-value stages. Loom does not choose
those Ikea implementations or add stages to ready work.
If changing width or a hint-policy snapshot changes the qualified crossover,
requalify the options and cohort. A hardware revision is not serial evidence.

`Epoch` is a full caller-supplied monotonic counter in a consistent unit, with
`max_age` in that unit. It measures elapsed time across idle intervals, rather
than just counting operations. The age limit is inclusive. Every selection/sampling
boundary rejects a changed identity, expiry or backward epoch; returning later
to an old identity/time cannot resurrect discarded evidence. Epoch wrap is a
backward transition. No native callback reads a clock or profile atomic.

`Paths` describes legal complete paths, useful independent requests and the
actual configured Ring width. Legality takes precedence. With both paths legal,
insufficient useful independence chooses direct. A caller-known direct `Prior`
also chooses direct without manufacturing serial evidence. Otherwise unknown
or expensive serial work chooses Ring, and qualified cheap samples choose direct.
When no choice is needed, callers can simply execute their known legal path.

## Samples and cadence

Call `sample_due(identity, now, paths, prior)` once per operation, optionally
execute a bounded serial probe, then call `select` for the useful operation.
`select` does not consume cadence. Calls with only one usable path, insufficient
independence or a known-direct prior request no sample and consume no cadence.
They still invalidate stale or mismatched evidence.

The caller owns the sample's grain, representativeness and subset rotation.
Complete one dependent chain before the next. Include all result-dependent
payload work; neither hint issuance nor pipelined reciprocal throughput is a
`SerialSample`. This measures software-serial complete-consumer cost with normal
CPU overlap, rather than one-outstanding-miss or isolated load latency.
Report its actual elapsed nanoseconds and completed request count
through `observe`, using the completion epoch. Zero work/time or an unqualified
identity is rejected without changing history. Cost is rounded upward safely;
any fraction above the cutoff is expensive. An expensive sample breaks the cheap
confirmation sequence. Charge the entire probe, its clocks and policy work to
the operation; repeated sample queries do not increase useful request counts.

An initial or incomplete cheap record requests another sample at the next
eligible boundary. The first expensive observation gets one early retry; a
second expensive observation backs off for `sample_interval` eligible calls.
Confirmed cheap evidence uses the same interval. If the interval is 32, calls
one through 31 after the observation are not due and call 32 is due. A skipped
or rejected requested sample remains due. A saturating countdown avoids a
wrapping operation counter. Identity, expiry and rollback reset the cadence.

This bounds retry cadence while evidence remains fresh. It is not a time or
work-rate limit: if evidence expires before every operation, every operation can
request a probe. A caller with no observation budget can skip it; without fresh
cheap evidence, selection stays on Ring when useful and legal. Small cheap
samples can miss heterogeneous costs, and still-fresh evidence can select direct
after unreported interference. The policy supplies no latency guarantee.

The [compiled example](../examples/execution.cpp) shows bounded genuine samples,
both complete paths, independent result checks and owner-lane cleanup. Its
explicit cutoff is illustrative. The
[graduation comparison](../../workbench/spikes/loom-locality-execution/GRADUATION.md)
retains charged complete-operation controls and actual native qualification.
The [original study](../../workbench/spikes/loom-locality-execution/FINDINGS.md)
also retains the rejected general unique-line contiguous selector. Memory hint
lowering remains separate from execution selection.
