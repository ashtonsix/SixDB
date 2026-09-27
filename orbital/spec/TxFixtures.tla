---------------------------- MODULE TxFixtures ----------------------------
EXTENDS Contracts, Integers

Params(scenario, bug, crash) ==
  LET txs==IF scenario \in {"single","own-read","undeclared","recovery"} THEN {1} ELSE IF scenario \in {"pair","cancel","migration","approved","async","readonly-check"} THEN 1..2 ELSE IF scenario="chain" THEN 1..4 ELSE 1..3
      shards==IF scenario="three-shard" THEN 1..3 ELSE IF scenario \in {"chain","recovery"} THEN {1} ELSE 1..2
      keys==1..4
      home==[k \in keys |-> IF scenario \in {"chain","recovery"} THEN 1 ELSE IF k=2 \/ (scenario="invalidation" /\ k=1) THEN 2 ELSE IF k=3 /\ scenario="three-shard" THEN 3 ELSE 1]
      writes==[t \in txs |-> IF t=1 THEN (IF scenario="own-read" THEN {1} ELSE {1,2}) ELSE IF t=2 THEN (IF scenario="readonly-check" THEN {} ELSE {1})
                             ELSE IF scenario="chain" /\ t=3 THEN {1} ELSE {}]
      reads==[t \in txs |-> IF t=1 THEN {} ELSE IF t=2 THEN {1}
                            ELSE IF scenario="chain" THEN (IF t=3 THEN {} ELSE {1})
                            ELSE IF scenario="three-shard" THEN {1,3} ELSE IF scenario="asymmetric" THEN {2} ELSE {1,2}]
      programs==[t \in txs |-> IF t=1 THEN (CASE scenario="chain" -> "none" [] scenario="own-read" -> "own-read" [] scenario="undeclared" -> "undeclared" [] OTHER -> "put")
               ELSE IF t=2 THEN (IF scenario="readonly-check" THEN "read" ELSE "increment")
               ELSE IF scenario="chain" THEN (IF t=3 THEN "put" ELSE "read") ELSE IF scenario="asymmetric" THEN "read" ELSE "sum"]
  IN [transactions |-> txs, shards |-> shards, keys |-> keys,
      home |-> home, writes |-> writes, reads |-> reads,
      parts |-> [t \in txs |-> {home[k]:k \in writes[t]}],
      owner |-> [t \in txs |-> 1],
      dependencies |-> [k \in keys |-> IF scenario="invalidation" /\ k=2 THEN {1,2} ELSE {k}],
      program |-> programs, firstRead |-> [t \in txs |-> IF scenario="asymmetric" /\ t=3 THEN 2 ELSE 1],
      secondRead |-> [t \in txs |-> IF scenario="three-shard" THEN 3 ELSE 2],
      value |-> [t \in txs |-> IF scenario="tombstone" /\ t=1 THEN -1 ELSE IF t=3 THEN 9 ELSE 1],
      causal |-> [t \in txs |-> IF scenario="causal" /\ t=2 THEN 12 ELSE IF scenario="asymmetric" /\ t=3 THEN 20 ELSE 0], profile |-> [runtime |-> "determinism-hardened",sourceVersion |-> "code-v1",representation |-> "canonical-1"], mapVersion |-> 1,
      checked |-> IF scenario \in {"checked","mismatch","readonly-check"} THEN {2} ELSE {},
      mismatch |-> scenario="mismatch", bug |-> bug, crash |-> crash, material |-> FALSE, physicalReads |-> FALSE, externalChecks |-> FALSE, externalCompute |-> {},
      cancel |-> scenario="cancel", migration |-> scenario="migration",
      approved |-> scenario="approved", asyncCheck |-> scenario="async", slow |-> IF scenario="chain" THEN {1} ELSE {}]
=============================================================================
