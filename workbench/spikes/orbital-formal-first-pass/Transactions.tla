------------------------- MODULE Transactions -------------------------
EXTENDS Integers, FiniteSets, TLC

(***************************************************************************
 A bounded semantic spine, not consensus: each shard action is an agreed,
 durable command. Delivery actions copy an existing durable record through an
 abstract retryable channel. Readers inspect only their shard's installed
 versions, local tickets and registered bounds, never the global decision.

 Application fixture: W=1 replaces x@A,y@B with 1. R=2 reserves z@A, reads
 x@A then discovers y@B at its immutable cut, and writes their sum to z.
 Conflict/invalidation are fixture relations, not Orbital knowledge of cells.
 The programs' output envelopes do not overlap: queue fairness is NOT covered.
 There is no complete-replacement shortcut for pending predecessors.

 Positions are generated from received minima, not an assigned transaction
 order. Residues distinguish identities; no physical clock/global sequencer.
 With two authored transactions the largest reachable position is 7; the
 implementation does not prune positions or disable actions at a clock bound.

 Aborts occur only after fixing/reading/computing, with a durable outcome.
 Pre-position cancellation, checked extensions, permanent authority loss and
 within-epoch reordering are outside this model. Crash episodes are bounded;
 shard durable metadata and coordinator decisions survive the positive reset.
***************************************************************************)

CONSTANTS Bad, ResetKind, CrashBudget, AbortAllowed
ASSUME Bad \in {"none", "skip-pending", "early-execution", "lost-bounds"}
ASSUME ResetKind \in {"none", "coordinator", "shard"}
ASSUME CrashBudget \in 0..1
ASSUME AbortAllowed \in BOOLEAN

Tx == {1, 2}
Shards == {1, 2}
Keys == {1, 2, 3}
ReadKeys == {1, 2}
Home(k) == IF k = 2 THEN 2 ELSE 1
Writes(t) == IF t = 1 THEN {1, 2} ELSE {3}
Parts(t) == {Home(k) : k \in Writes(t)}
LocalWrites(t, s) == {k \in Writes(t) : Home(k) = s}
Conflicts(t, u, s) == LocalWrites(t, s) \cap LocalWrites(u, s) # {}
Invalidates(t, k) == k \in Writes(t)
Max(S) == CHOOSE n \in S : \A m \in S : n >= m
MinFor(t, n) == 3 * ((n \div 3) + 1) + t

VARIABLES ticket, minimum, localC, bound, registered, installed, stored,
          position, fixAcks, scratch, outcome, decision, learned, complete,
          reads, observed, coordinatorUp, shardUp, crashesLeft, crashRecord
vars == <<ticket, minimum, localC, bound, registered, installed, stored,
          position, fixAcks, scratch, outcome, decision, learned, complete,
          reads, observed, coordinatorUp, shardUp, crashesLeft, crashRecord>>

Init ==
  /\ ticket = [t \in Tx |-> [s \in Shards |-> "none"]]
  /\ minimum = [t \in Tx |-> [s \in Shards |-> 0]]
  /\ localC = [t \in Tx |-> [s \in Shards |-> 0]]
  /\ bound = [k \in Keys |-> 0]
  /\ registered = {}
  /\ installed = [k \in Keys |-> 0]
  /\ stored = [k \in Keys |-> 0]
  /\ position = [t \in Tx |-> 0]
  /\ fixAcks = [t \in Tx |-> {}]
  /\ scratch = [t \in Tx |-> -1]
  /\ outcome = [t \in Tx |-> -1]
  /\ decision = [t \in Tx |-> "none"]
  /\ learned = [t \in Tx |-> [s \in Shards |-> [verdict |-> "none", value |-> -1]]]
  /\ complete = {}
  /\ reads = [k \in ReadKeys |-> -1]
  /\ observed = [k \in ReadKeys |-> {}]
  /\ coordinatorUp = [t \in Tx |-> TRUE]
  /\ shardUp = [s \in Shards |-> TRUE]
  /\ crashesLeft = CrashBudget
  /\ crashRecord = <<0, 0, "none">>

Held(t, s) == ticket[t][s] \in {"held", "announced"}
HasAllReservations(t) == \A s \in Parts(t) : Held(t, s)
HasAllAnnouncements(t) == \A s \in Parts(t) : minimum[t][s] > 0
FixedEverywhere(t) == \A s \in Parts(t) : localC[t][s] = position[t]
ResolvedEverywhere(t) == \A s \in Parts(t) : ticket[t][s] = "resolved"

Acquire(t, s) ==
  /\ s \in Parts(t) /\ shardUp[s] /\ coordinatorUp[t]
  /\ ticket[t][s] = "none"
  /\ \A earlier \in Parts(t) : earlier < s => ticket[t][earlier] # "none"
  /\ \A u \in Tx \ {t} : Conflicts(t, u, s) => ~Held(u, s)
  /\ ticket' = [ticket EXCEPT ![t][s] = "held"]
  /\ UNCHANGED <<crashRecord, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

LocalFloor(t, s) ==
  Max({0} \cup {bound[k] : k \in LocalWrites(t, s)} \cup
      {IF localC[u][s] > 0 THEN localC[u][s] ELSE minimum[u][s] :
         u \in {v \in Tx \ {t} : Conflicts(t, v, s)}})

Announce(t, s) ==
  /\ s \in Parts(t) /\ shardUp[s] /\ coordinatorUp[t]
  /\ ticket[t][s] = "held" /\ HasAllReservations(t)
  /\ minimum' = [minimum EXCEPT ![t][s] = MinFor(t, LocalFloor(t, s))]
  /\ ticket' = [ticket EXCEPT ![t][s] = "announced"]
  /\ UNCHANGED <<crashRecord, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

(** Delivery of the complete set of minimum replies and persistence of c.
    Split participant announcements remain visible while replies are delayed.
    Only replies to this transaction determine c; no global freshness oracle. *)
ChoosePosition(t) ==
  /\ coordinatorUp[t] /\ HasAllAnnouncements(t) /\ position[t] = 0
  /\ position' = [position EXCEPT ![t] = Max({minimum[t][s] : s \in Parts(t)})]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

(** A durable local fix releases that reservation immediately. No guard asks
    whether another shard has received its fix or returned its acknowledgement. *)
DeliverFix(t, s) ==
  /\ s \in Parts(t) /\ coordinatorUp[t] /\ shardUp[s]
  /\ position[t] > 0 /\ ticket[t][s] = "announced"
  /\ localC' = [localC EXCEPT ![t][s] = position[t]]
  /\ ticket' = [ticket EXCEPT ![t][s] = "fixed"]
  /\ UNCHANGED <<crashRecord, minimum, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

DeliverFixAck(t, s) ==
  /\ s \in Parts(t) /\ coordinatorUp[t] /\ shardUp[s]
  /\ localC[t][s] > 0 /\ s \notin fixAcks[t]
  /\ fixAcks' = [fixAcks EXCEPT ![t] = @ \cup {s}]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

RegisterRead(k) ==
  /\ k \in ReadKeys /\ coordinatorUp[2] /\ shardUp[Home(k)]
  /\ fixAcks[2] = Parts(2) /\ k \notin registered
  /\ k = 1 \/ reads[1] >= 0
  /\ registered' = registered \cup {k}
  /\ bound' = [bound EXCEPT ![k] = Max({@, position[2]})]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

LocallyPending(t, k, c) ==
  /\ Invalidates(t, k)
  /\ ticket[t][Home(k)] \in {"announced", "fixed"}
  /\ (IF localC[t][Home(k)] > 0
       THEN localC[t][Home(k)] <= c ELSE minimum[t][Home(k)] <= c)

LocalValue(k, c) ==
  IF installed[k] = 0 THEN 0
  ELSE IF localC[installed[k]][Home(k)] <= c THEN stored[k] ELSE 0

Read(k) ==
  /\ k \in ReadKeys /\ coordinatorUp[2] /\ shardUp[Home(k)]
  /\ k \in registered /\ reads[k] = -1
  /\ Bad = "skip-pending" \/ \A t \in Tx \ {2} : ~LocallyPending(t, k, position[2])
  /\ reads' = [reads EXCEPT ![k] = LocalValue(k, position[2])]
  /\ observed' = [observed EXCEPT ![k] = @ \cup {LocalValue(k, position[2])}]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       coordinatorUp, shardUp, crashesLeft>>

Compute(t) ==
  /\ coordinatorUp[t] /\ position[t] > 0 /\ scratch[t] = -1 /\ outcome[t] = -1
  /\ Bad = "early-execution" \/ fixAcks[t] = Parts(t)
  /\ t = 1 \/ \A k \in ReadKeys : reads[k] >= 0
  /\ scratch' = [scratch EXCEPT ![t] = IF t = 1 THEN 1 ELSE reads[1] + reads[2]]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

PersistOutcome(t) ==
  /\ coordinatorUp[t] /\ scratch[t] >= 0 /\ outcome[t] = -1
  /\ outcome' = [outcome EXCEPT ![t] = scratch[t]]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, decision, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

Decide(t, d) ==
  /\ coordinatorUp[t] /\ outcome[t] >= 0 /\ decision[t] = "none"
  /\ d = "commit" \/ (AbortAllowed /\ d = "abort")
  /\ decision' = [decision EXCEPT ![t] = d]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, learned, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

DeliverDecision(t, s) ==
  /\ s \in Parts(t) /\ coordinatorUp[t] /\ shardUp[s]
  /\ decision[t] # "none" /\ learned[t][s].verdict = "none"
  /\ learned' = [learned EXCEPT ![t][s] = [verdict |-> decision[t], value |-> outcome[t]]]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, complete,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

Install(t, s) ==
  /\ s \in Parts(t) /\ shardUp[s] /\ ticket[t][s] = "fixed"
  /\ learned[t][s].verdict # "none"
  /\ ticket' = [ticket EXCEPT ![t][s] = "resolved"]
  /\ installed' = [k \in Keys |->
       IF k \in LocalWrites(t, s) /\ learned[t][s].verdict = "commit" THEN t ELSE installed[k]]
  /\ stored' = [k \in Keys |->
       IF k \in LocalWrites(t, s) /\ learned[t][s].verdict = "commit" THEN learned[t][s].value ELSE stored[k]]
  /\ UNCHANGED <<crashRecord, minimum, localC, bound, registered, position, fixAcks,
       scratch, outcome, decision, learned, complete, reads, observed,
       coordinatorUp, shardUp, crashesLeft>>

(** Coalesced delivery of installation receipts; does not grant read access. *)
Finish(t) ==
  /\ coordinatorUp[t] /\ ResolvedEverywhere(t) /\ t \notin complete
  /\ complete' = complete \cup {t}
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned,
       reads, observed, coordinatorUp, shardUp, crashesLeft>>

CrashCoordinator(t) ==
  /\ ResetKind = "coordinator" /\ crashesLeft > 0 /\ coordinatorUp[t]
  /\ position[t] > 0 /\ t \notin complete
  /\ coordinatorUp' = [coordinatorUp EXCEPT ![t] = FALSE]
  /\ fixAcks' = [fixAcks EXCEPT ![t] = {}]
  /\ scratch' = [scratch EXCEPT ![t] = -1]
  /\ reads' = IF t = 2 THEN [k \in ReadKeys |-> -1] ELSE reads
  /\ crashesLeft' = crashesLeft - 1
  /\ crashRecord' = <<t, position[t], IF decision[t] # "none" THEN "decision"
                        ELSE IF outcome[t] >= 0 THEN "outcome"
                        ELSE IF t = 1 /\ localC[t][1] > 0 /\ localC[t][2] = 0 THEN "partial-fix"
                        ELSE IF ~FixedEverywhere(t) THEN "before-fix" ELSE "fixed">>
  /\ UNCHANGED <<ticket, minimum, localC, bound, registered, installed, stored,
       position, outcome, decision, learned, complete, observed, shardUp>>

RecoverCoordinator(t) ==
  /\ ~coordinatorUp[t]
  /\ coordinatorUp' = [coordinatorUp EXCEPT ![t] = TRUE]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, shardUp, crashesLeft>>

CrashShard(s) ==
  /\ ResetKind = "shard" /\ crashesLeft > 0 /\ shardUp[s]
  /\ \E k \in ReadKeys : Home(k) = s /\ observed[k] # {}
  /\ shardUp' = [shardUp EXCEPT ![s] = FALSE]
  /\ bound' = [k \in Keys |-> IF Bad = "lost-bounds" /\ Home(k) = s THEN 0 ELSE bound[k]]
  /\ crashesLeft' = crashesLeft - 1
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp>>

RecoverShard(s) ==
  /\ ~shardUp[s]
  /\ shardUp' = [shardUp EXCEPT ![s] = TRUE]
  /\ UNCHANGED <<crashRecord, ticket, minimum, localC, bound, registered, installed, stored,
       position, fixAcks, scratch, outcome, decision, learned, complete,
       reads, observed, coordinatorUp, crashesLeft>>

Done == complete = Tx /\ (\A t \in Tx : coordinatorUp[t]) /\ (\A s \in Shards : shardUp[s])
Terminal == Done /\ UNCHANGED vars
Next ==
  \/ \E t \in Tx, s \in Shards : Acquire(t, s) \/ Announce(t, s) \/ DeliverFix(t, s)
       \/ DeliverFixAck(t, s) \/ DeliverDecision(t, s) \/ Install(t, s)
  \/ \E t \in Tx : ChoosePosition(t) \/ Compute(t) \/ PersistOutcome(t)
       \/ Decide(t, "commit") \/ Decide(t, "abort") \/ Finish(t)
       \/ CrashCoordinator(t) \/ RecoverCoordinator(t)
  \/ \E k \in ReadKeys : RegisterRead(k) \/ Read(k)
  \/ \E s \in Shards : CrashShard(s) \/ RecoverShard(s)
  \/ Terminal

SafetySpec == Init /\ [][Next]_vars
Fairness ==
  /\ \A t \in Tx, s \in Shards :
       /\ WF_vars(Acquire(t, s)) /\ WF_vars(Announce(t, s))
       /\ WF_vars(DeliverFix(t, s)) /\ WF_vars(DeliverFixAck(t, s))
       /\ WF_vars(DeliverDecision(t, s)) /\ WF_vars(Install(t, s))
  /\ \A t \in Tx :
       /\ WF_vars(ChoosePosition(t)) /\ WF_vars(Compute(t))
       /\ WF_vars(PersistOutcome(t)) /\ WF_vars(Decide(t, "commit"))
       /\ WF_vars(Finish(t)) /\ WF_vars(RecoverCoordinator(t))
  /\ \A k \in ReadKeys : WF_vars(RegisterRead(k)) /\ WF_vars(Read(k))
  /\ \A s \in Shards : WF_vars(RecoverShard(s))
Spec == SafetySpec /\ Fairness
EventuallyComplete == <>Done

(***************************************************************************
 Observer definitions below are never used in an action guard. The read
 oracle must reject an unfinished lower predecessor immediately, as well as
 checking the value against the independent application reference. Comparing
 only final decisions would miss a bad read while that predecessor is stalled.
 Observed is a bounded ghost set, preserving completed observations on reset.
***************************************************************************)
TypeOK ==
  /\ ticket \in [Tx -> [Shards -> {"none", "held", "announced", "fixed", "resolved"}]]
  /\ minimum \in [Tx -> [Shards -> Nat]] /\ localC \in [Tx -> [Shards -> Nat]]
  /\ bound \in [Keys -> Nat] /\ registered \subseteq ReadKeys
  /\ installed \in [Keys -> (Tx \cup {0})] /\ stored \in [Keys -> 0..2]
  /\ position \in [Tx -> Nat] /\ fixAcks \in [Tx -> SUBSET Shards]
  /\ scratch \in [Tx -> -1..2] /\ outcome \in [Tx -> -1..2]
  /\ decision \in [Tx -> {"none", "commit", "abort"}]
  /\ learned \in [Tx -> [Shards -> [verdict : {"none", "commit", "abort"}, value : -1..2]]]
  /\ complete \subseteq Tx /\ reads \in [ReadKeys -> -1..1]
  /\ observed \in [ReadKeys -> SUBSET {0, 1}]
  /\ coordinatorUp \in [Tx -> BOOLEAN] /\ shardUp \in [Shards -> BOOLEAN]
  /\ crashesLeft \in 0..CrashBudget
  /\ crashRecord \in (Tx \cup {0}) \X Nat \X {"none", "before-fix", "partial-fix", "fixed", "outcome", "decision"}

OnePosition ==
  /\ (position[1] > 0 /\ position[2] > 0) => position[1] # position[2]
  /\ \A t \in Tx : \A s \in Parts(t) :
       /\ localC[t][s] > 0 => localC[t][s] = position[t]
       /\ position[t] > 0 => position[t] >= minimum[t][s]
PositionSurvivesReset == crashRecord[1] # 0 => position[crashRecord[1]] = crashRecord[2]
NoEarlyExecution == \A t \in Tx : scratch[t] >= 0 => fixAcks[t] = Parts(t)
DecisionHasOutcome == \A t \in Tx : decision[t] # "none" =>
  outcome[t] >= 0 /\ position[t] > 0 /\ FixedEverywhere(t)
BoundSurvivesRead == \A k \in ReadKeys : observed[k] # {} => bound[k] >= position[2]
NoSkippedPending == \A k \in ReadKeys : observed[k] # {} =>
  ((minimum[1][Home(k)] > 0 /\ minimum[1][Home(k)] <= position[2] /\
    (position[1] = 0 \/ position[1] <= position[2])) =>
   (decision[1] # "none" /\ learned[1][Home(k)].verdict = decision[1] /\ ticket[1][Home(k)] = "resolved"))

(** This fixture's independently evaluated serial program: W supplies 1 at
    both source cells iff it commits before R. R's actual sum is not consulted. *)
ReferenceSource == IF position[1] > 0 /\ position[1] < position[2] /\ decision[1] = "commit"
                   THEN 1 ELSE 0
SerialObservations == \A k \in ReadKeys : observed[k] \subseteq {ReferenceSource}
SerialOutcome == outcome[2] >= 0 => outcome[2] = 2 * ReferenceSource
InstalledDecision == \A k \in Keys : installed[k] # 0 =>
  LET t == installed[k] IN decision[t] = "commit" /\ stored[k] = outcome[t]
CompletedDecision == \A t \in complete : decision[t] # "none" /\ ResolvedEverywhere(t)

(** Deliberately false reachability invariants; witnesses are successful when
    TLC reports their named violation. They are not safety failures. *)
NoPartialFix == ~(localC[1][1] > 0 /\ localC[1][2] = 0)
NoPartialInstall == ~(ticket[1][1] = "resolved" /\ ticket[1][2] = "fixed")
NoReadBeforeAnnouncement == ~(observed[1] # {} /\ minimum[1][1] = 0)
NoAbort == \A t \in Tx : decision[t] # "abort"
NoOutcomeRecovery == ~(crashRecord[3] = "outcome" /\ coordinatorUp[crashRecord[1]])
NoPositionRecoveryComplete == ~(crashRecord[1] = 1 /\ crashRecord[3] = "partial-fix" /\
                               1 \in complete /\ coordinatorUp[1])
NoDecisionRecoveryComplete == ~(crashRecord[3] = "decision" /\
                               crashRecord[1] \in complete /\ coordinatorUp[crashRecord[1]])
NoReadAcrossPartialInstall == ~(reads[1] = 1 /\ 2 \in registered /\ reads[2] = -1 /\
                              ticket[1][1] = "resolved" /\ ticket[1][2] = "fixed")
NoPublication == complete = {}
=============================================================================
