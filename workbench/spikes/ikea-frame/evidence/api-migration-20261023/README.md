# Current Frame consumer API migration

The current Frame code, including predicate cut `3f31a329`, uses Ikea's byte
views, layouts, readers/writers, authored expressions and flat effects journal.
The fifteen changed files preserve private u8 payloads, descriptor bytes,
arithmetic, selections, dictionary editions, checkpoint policies and operation
programs. No Frame representation or public interface is selected here.

The [settled six-file port](../../../ikea-composition/evidence/interface-cohesion-v2-20261023/frame/README.md)
supplied the boundary changes and its older paired controls. This migration
also covers the current predicate emitter, point/checkpoint consumers, separated
filter heads and fitting-repair effects. Named views and bindings outlive their
calls; journals resolve while their source mapping is still valid. No operation
state is added to published bytes.

[Qualification](qualification.json) records exact source, input and executable
identities. The frozen NEON run passes the existing 82 owner/oracle commands and
72 additional commands: current predicate A→B pairs across checkpoint/dictionary
options, point/delta repairs, protected filter planes and the actual hash-output
oracle. Its 3,200 complete owner records all report verification. All 3,569
captured source hashes match the worker. AVX2/AVX-512 qualification is still in
progress in the named job; this note does not claim it has passed.

The earlier live build and its local checks were diagnostic: seven shared Ikea
headers changed during compilation. Qualified runs use frozen captures instead.
The first Zen 5 attempt compiled AVX2 but failed in the runner's feature-report
command (`zen5` instead of Clang's `znver5`); no oracle checks ran there. The
retry changes only that runner, not Frame or Ikea code. No qualification time
is a throughput measurement, and historical timing cohorts remain unchanged.

The worker artifact contains frozen sources, commands, compile flags, actual
dependency lists, input receipts and complete logs. The runner reuses the
settled qualifier with its input-manifest path relocated; additional commands
invoke existing independently checked programs. General reusable-control
research and later Ikea dependency changes remain separately qualified work.
