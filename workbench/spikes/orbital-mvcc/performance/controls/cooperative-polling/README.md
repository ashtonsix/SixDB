# Cooperative service during selected Object reads

This disposable caller asks how much of the ready-read Ring result survives
charging cooperative Message service, and how long ready completions wait.
It uses the selected ObjectStore, actual Loom Ring/Worker and existing dependent
Tuple read bodies. Orbital and Loom APIs, native callbacks, admission, refill and
request interpretation are unchanged. The disposable `Driver::run` accepts one
optional concrete `CompletionBurst` pointer, used only after a quantum returns.
It is not a scheduler or a general callback extension.

Each offered group posts eight caller-owned Messages to the same owner Lane
before opening requests. Each handler consumes eight authored words, records a
checksum and delivery order, then the actual queue releases its retained use.
There is no external producer or ongoing arrival process. Payloads and Message
records are fixed reusable inventory; nothing is allocated during service.
The handler does not mutate the catalog, read frames, Ring or selected bytes.

The unloaded arm uses this adapted caller, including its optional argument/branch
and common burst reset. It is not byte-identical to the old performance executable.

The six treatments hold width32, hints enabled and sixteen links per request:

| Treatment | Execution | Quantum | Message service |
| --- | --- | ---: | --- |
| unloaded | Ring | 128 | No Messages or poll calls |
| empty | Ring | 128 | Empty poll after each returned quantum |
| loaded | Ring | 128 | Eight ready Messages; at most one per returned quantum |
| deferred | Ring | 128 | Same eight Messages; all service after read completion |
| loaded | Ring | 32 | Same load, shorter native quantum |
| loaded | manual | 128 | Same staged law, rotation, refill and service boundary |

Polls happen immediately after native/manual work returns, before retiring and
refilling frames. Final drain always belongs to the group, including the short
last group. No Message is reposted until the previous group has fully drained.
An RAII drain also keeps the fixed records alive through exception cleanup.

## Clocks and independent checks

Every group starts its work clock before posting and stops it after reads plus
all Message handling/releases. `read_complete_ns` stops at read completion;
`all_work_complete_ns` includes final service. Both totals sum measured group
intervals, excluding between-group oracle checks and result aggregation. The
extra read-completion observation itself lies inside the complete-work interval.
Per-quantum service-count validation, handler ownership checks, FIFO recording,
checksum work and retain/handle/release bookkeeping are also inside the clocks.
The empty-poll comparison therefore charges this control machinery as well as
polling; it is not an isolated poll-instruction cost.
Caller/process CPU totals use the same group boundaries but also include their
clock-call overhead. Setup, configuration, warm-up, cold sample preparation,
final Driver close and fixture teardown are separate from the work interval.

Throughput mode reads no clock inside post/handler callbacks. Separate `delay`
mode timestamps immediately before posting and at the end of the handler,
before the queue's release callback. Thus delivery delay includes post work,
queue waiting and checksum work; it is not an external I/O latency or the final
queue-release timestamp. Complete-work time includes that release. The mode's
additional clocks are charged and its timings must not be pooled with throughput.
Samples are group makespans and Message delays, never request latency.

Reads still match the independent unencoded map oracle. Message IDs must match
the authored FIFO sequence; each record must have exactly one retain, handle and
release, and the eight-word sums must match an independent closed-form formula.
The fully posted queue must have delivered `min(returned_quanta,8)` Messages
before final drain in the loaded treatment. This bounds service opportunities,
not elapsed time. Empty/unloaded controls must handle zero Messages. Ordinary
throughput records must contain no delivery timestamps.

`--verify-only 1` disables group/CPU clocks while running all logical checks.
It does not disable delivery timestamps when `--mode delay` is selected.
`--refusal-check 1` also forces an absent Object during replacement admission,
after resident work, drains the Messages, closes/reconfigures the Driver and
then verifies that it remains reusable. There is no cancellation API in this
synchronous caller; no cancellation mechanism has been invented for the test.

## State and running

On the qualified Linux AArch64 build, one Message record is 80 B and the complete
burst control is **1,328 B**: 640 B records, 512 B useful payload, 64 B expected
checksums, 64 B observed order, 40 B owner/counters and 8 B mode/flags/padding.
The shared static `MessageOps` table adds 24 B. These are fixture costs, not a
proposed production completion packet. The existing Driver stays 11,712 B;
its separate measured Ring Arena charge is reported. The optional refusal check
uses another temporary control and request/output arrays outside measured work.

Each group also reserves 56 B work samples and 64 B delivery sample storage,
identically in both modes. At 513 groups these are 28,728 B and 32,832 B. Output,
input, corpus and oracle inventories remain the existing benchmark's cold work.
There are no PMU, general fairness, external publication, Task, Signal fanout,
Engine/Shore or sustained-arrival claims.

Build only `loom_object_polling_control` with the ordinary `orbital-mvcc` spike
configuration and pinned compiler. `run.py` executes the six throughput arms and
four loaded diagnostic arms once each, with new JSON and CSV files. Its default
is the prior large-body case: 8,388,608 rows across 256 Objects, 524,291 requests,
1,024-request groups and a three-request final group. It launches no worker.

```sh
python3 workbench/spikes/orbital-mvcc/performance/controls/cooperative-polling/run.py \
  --binary BUILD/workbench/spikes/orbital-mvcc/performance/loom_object_polling_control \
  --output NEW_OUTPUT_DIRECTORY
```

Add `--small` for a quick 32,768-row, 2,051-request local signal. One observation
per arm is not a stable ranking or tail estimate; retain order and exact binary
identity when arranging a later matched comparison.
