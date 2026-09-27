# Orbital first formal investigation

Superseded first investigation, retained as a spike. Its planning did not cover
Orbital’s complete correctness-critical composition. The current restart belongs
in the [verification plan](../../../orbital/spec/PLAN.md). No model or result here
counts as completion of that plan.

Small TLA+ models of consequential ordering, verification and recovery questions.
The [reviewed plan](PLAN.md) explains the boundaries and open design choices;
the [findings](FINDINGS.md) record counterexamples, measured graph growth and
what is still unproved. These are bounded model checks, not a proof of the
whole architecture or the simulator's implementation.

| Model | What is checked |
| --- | --- |
| [Transactions](Transactions.tla) | Generated positions, registered reads, actual serial observations, partial installation and surviving-storage recovery. |
| [Reservations](Reservations.tla) | Overlapping queues, local release and conflicting positions; separate from application execution. |
| [CheckedReplay](CheckedReplay.tla) | Durable old-read grants, reconstruction after reset, complete interaction evidence and a verified read-only result. |
| [AdmissionCoverage](AdmissionCoverage.tla) | Contiguous producer ranges, actual payload/decoder durability and the limits of historical receipts. |
| [RetentionClosure](RetentionClosure.tla) | Reader/replay/head dependencies, checkpoint publication and a proposed serialized root-registration protocol. |
| [BorrowLifetime](BorrowLifetime.tla) | Physical users surviving cancellation/owner death, generation reuse and callback identity. |

## Run locally

From the repository root, using Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/orbital-formal-first-pass/suite.py --tier quick --output build/orbital-spec/quick
python3 workbench/spikes/orbital-formal-first-pass/suite.py --tier growth --output build/orbital-spec/growth
python3 workbench/spikes/orbital-formal-first-pass/suite.py --tier scaling --output build/orbital-spec/scaling
```

Use a new output directory for each run. The [case catalog](cases.json) names
ordinary checks, deliberately broken variants and reachability witnesses.
`--case NAME` selects a case; repeated options select several. The quick suite
runs sequentially with one worker and a 512 MiB heap. Growth increases individual
model dimensions; scaling repeats one unchanged case with four workers.
`--timeout` sets each case's observation budget. A timeout remains incomplete.

For an individual case or a new configuration:

```sh
python3 workbench/spikes/orbital-formal-first-pass/check.py --module workbench/spikes/orbital-formal-first-pass/Transactions.tla \
  --config workbench/spikes/orbital-formal-first-pass/configs/Transactions.cfg \
  --output build/orbital-spec/individual --timeout 60
```

The runner uses the [shared TLC pin](../../tools/tlc/source.json),
resolving verified local bytes or the shared input cache. It captures model and
configuration bytes, tool/JVM identity, logs, progress, per-process peak RSS and
completion status. Deadlock checking stays enabled. An expected counterexample
must name the actual violated property; arbitrary tool failure is not success.
Reachability witnesses are separately labelled expected violations of deliberately
false predicates. Temporal controls contain one named property so an otherwise
unnamed TLC temporal-error diagnostic is unambiguous.

The suite freezes model inputs once and retains every case disposition in
`summary.json`; each case also has its own full receipt and raw counterexample.
Run `python3 workbench/spikes/orbital-formal-first-pass/test_check.py` for the small runner classification tests.
Scaffolding owns [worker checkpoint/recovery support](../../tools/tlc/README.md).
No remote machines are needed for the initial checks. Hardware sizing should
follow completed adjacent instances, not initial states-per-second estimates.

## Relation to implementation

The models import narrow service contracts and state those in their headers.
For example, Transactions imports ordered durable shard commands; it does not
reprove consensus. Admission imports that journal while checking the evidence
offered to it. Retention's drain service is a candidate prompted by a counterexample,
not a newly implemented runtime port. Ordinary terminal stuttering and finite
fault budgets are explicit. No symmetry or hidden state constraint is used.

The [native models](../../simulator/models/README.md) remain the place
to explore physical costs and actor implementations. `DeliverFix` corresponds
to a shard's durable position fix, `Install` to applying its learned outcome,
and `Read` to a bound-registered observation. Those abstract transitions may each
require several simulator events. Retention's storage tokens correspond to the
base/decoder/checkpoint/suffix reads checked by the native reconstruction oracle;
BorrowLifetime separates physical completion from callback delivery as the native
runtime does. This is an intended mapping, not an implemented trace-refinement
checker or proof.

The model/configuration and retained evidence bytes are preserved. Relocation
changed documentation links and repository discovery in the runner; original
runner bytes remain recoverable in the captured evidence bundles.
