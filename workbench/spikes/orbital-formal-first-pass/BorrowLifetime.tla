--------------------------- MODULE BorrowLifetime ---------------------------
EXTENDS Naturals, FiniteSets

(* One physical slot, two one-shot users, two owner incarnations. Submission
   borrows immutable bytes; the queued request, backend, and undrained result
   each retain that borrow. Cancellation is only a request until the backend
   acknowledges it. A process crash does not cancel the backend. A device reset
   is deliberately excluded from this leaf: its confirmed cancellation must
   discharge each backend obligation, not merely discard the owner.

   Generation is a symbolic byte identity; no CPU memory ordering, UFFD, or
   logical old-version reconstruction is proved. The implementation's buffer
   identities/borrow counts refine this service boundary, not its allocator.
   Each user submits at most once; terminal stuttering is explicit. *)

CONSTANTS Mode, NumUsers
Users == 1..NumUsers
Phases == {"unused", "queued", "active", "complete", "retired"}
Modes == {"safe", "drop-on-death", "drop-on-first", "drop-on-cancel",
          "stale-callback", "omit-service"}
ASSUME /\ Mode \in Modes /\ NumUsers \in 1..3

VARIABLES generation, incarnation, alive, owner, closing, allocated,
          phase, pins, capturedGeneration, capturedIncarnation, cancelled,
          resultGeneration, deliveredIncarnation, heartbeat
vars == <<generation, incarnation, alive, owner, closing, allocated,
          phase, pins, capturedGeneration, capturedIncarnation, cancelled,
          resultGeneration, deliveredIncarnation, heartbeat>>

Needed == {u \in Users : phase[u] \in {"queued", "active", "complete"}}
AllRetired == \A u \in Users : phase[u] = "retired"

Init ==
  /\ generation = 1 /\ incarnation = 1
  /\ alive = TRUE /\ owner = TRUE /\ allocated = TRUE /\ closing = FALSE
  /\ phase = [u \in Users |-> "unused"] /\ pins = {}
  /\ capturedGeneration = [u \in Users |-> 0]
  /\ capturedIncarnation = [u \in Users |-> 0]
  /\ cancelled = {}
  /\ resultGeneration = [u \in Users |-> 0]
  /\ deliveredIncarnation = [u \in Users |-> 0]
  /\ heartbeat = FALSE

Submit(u) ==
  /\ alive /\ owner /\ allocated /\ ~closing /\ phase[u] = "unused"
  /\ phase' = [phase EXCEPT ![u] = "queued"]
  /\ pins' = pins \cup {u}
  /\ capturedGeneration' = [capturedGeneration EXCEPT ![u] = generation]
  /\ capturedIncarnation' = [capturedIncarnation EXCEPT ![u] = incarnation]
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  cancelled, resultGeneration, deliveredIncarnation, heartbeat>>

Start(u) ==
  /\ phase[u] = "queued"
  /\ phase' = [phase EXCEPT ![u] = "active"]
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  pins, capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation, heartbeat>>

BackendComplete(u) ==
  /\ phase[u] = "active"
  /\ phase' = [phase EXCEPT ![u] = "complete"]
  /\ resultGeneration' = [resultGeneration EXCEPT
                           ![u] = IF allocated THEN generation ELSE 0]
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  pins, capturedGeneration, capturedIncarnation, cancelled,
                  deliveredIncarnation, heartbeat>>

(* This is the action used both by Next and by temporal fairness. The negative
   removes service from BOTH; fair nonexistent service must not mask the stall. *)
Service(u) == /\ Mode # "omit-service" /\ BackendComplete(u)

Cancel(u) ==
  /\ phase[u] \in {"queued", "active"} /\ u \notin cancelled
  /\ cancelled' = cancelled \cup {u}
  /\ pins' = IF Mode = "drop-on-cancel" THEN pins \ {u} ELSE pins
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  phase, capturedGeneration, capturedIncarnation,
                  resultGeneration, deliveredIncarnation, heartbeat>>

BackendCancel(u) ==
  /\ u \in cancelled /\ phase[u] \in {"queued", "active"}
  /\ phase' = [phase EXCEPT ![u] = "complete"]
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  pins, capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation, heartbeat>>

Drain(u) ==
  /\ phase[u] = "complete"
  /\ phase' = [phase EXCEPT ![u] = "retired"]
  /\ pins' = IF Mode = "drop-on-first" THEN {} ELSE pins \ {u}
  /\ deliveredIncarnation' = [deliveredIncarnation EXCEPT ![u] =
       IF alive /\ (capturedIncarnation[u] = incarnation \/ Mode = "stale-callback")
       THEN incarnation ELSE 0]
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, heartbeat>>

Close ==
  /\ alive /\ ~closing /\ closing' = TRUE
  /\ UNCHANGED <<generation, incarnation, alive, owner, allocated, phase, pins,
                  capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation, heartbeat>>

ReleaseOwner ==
  /\ owner /\ closing /\ owner' = FALSE
  /\ UNCHANGED <<generation, incarnation, alive, closing, allocated, phase,
                  pins, capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation, heartbeat>>

Crash ==
  /\ alive /\ incarnation = 1 /\ alive' = FALSE /\ owner' = FALSE /\ closing' = TRUE
  /\ pins' = IF Mode = "drop-on-death" THEN {} ELSE pins
  /\ UNCHANGED <<generation, incarnation, allocated, phase, capturedGeneration,
                  capturedIncarnation, cancelled, resultGeneration,
                  deliveredIncarnation, heartbeat>>

Restart ==
  /\ ~alive /\ incarnation = 1 /\ alive' = TRUE /\ incarnation' = 2
  /\ UNCHANGED <<generation, owner, closing, allocated, phase, pins,
                  capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation, heartbeat>>

Free ==
  /\ allocated /\ ~owner /\ pins = {} /\ allocated' = FALSE
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, phase, pins,
                  capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation, heartbeat>>

Reuse ==
  /\ alive /\ ~allocated /\ generation = 1
  /\ allocated' = TRUE /\ owner' = TRUE /\ closing' = FALSE /\ generation' = 2
  /\ UNCHANGED <<incarnation, alive, phase, pins, capturedGeneration,
                  capturedIncarnation, cancelled, resultGeneration,
                  deliveredIncarnation, heartbeat>>

Next ==
  \/ \E u \in Users : Submit(u) \/ Start(u) \/ Service(u) \/ Cancel(u)
                         \/ BackendCancel(u) \/ Drain(u)
  \/ Close \/ ReleaseOwner \/ Crash \/ Restart \/ Free \/ Reuse
  \/ (closing /\ ~owner /\ ~allocated /\ Needed = {} /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ generation \in 1..2 /\ incarnation \in 1..2
  /\ alive \in BOOLEAN /\ owner \in BOOLEAN /\ closing \in BOOLEAN
  /\ allocated \in BOOLEAN /\ phase \in [Users -> Phases]
  /\ pins \subseteq Users /\ cancelled \subseteq Users
  /\ capturedGeneration \in [Users -> 0..2]
  /\ capturedIncarnation \in [Users -> 0..2]
  /\ resultGeneration \in [Users -> 0..2]
  /\ deliveredIncarnation \in [Users -> 0..2]
  /\ heartbeat \in BOOLEAN

(* Needed is independently derived from actual operation lifecycle, not pins. *)
LiveUsersCharged == Needed \subseteq pins /\ (Needed # {} => allocated)
SealedBytesStable == \A u \in Needed : generation = capturedGeneration[u]
ResultIdentity == \A u \in Users : resultGeneration[u] # 0 =>
                                      resultGeneration[u] = capturedGeneration[u]
CallbackIncarnation == \A u \in Users : deliveredIncarnation[u] # 0 =>
                         deliveredIncarnation[u] = capturedIncarnation[u]

(* An intentionally false invariant for a dedicated reachability run. Its
   counterexample witnesses two old users, cancellation, crash, and safe reuse. *)
NoRecoveryWitness == ~(incarnation = 2 /\ generation = 2 /\ AllRetired /\
  cancelled # {} /\ \A u \in Users : capturedGeneration[u] = 1 /\
                                     capturedIncarnation[u] = 1)

(* Tiny progress fixture: both borrows already admitted, owner has closed and
   released. No more arrivals, faults, cancellation, or reuse. Every concrete
   queued service, drain and free transition is weakly fair. A separate two-state
   housekeeping action remains runnable even if backend service is omitted; the
   progress control can then expose a fair cycle with unfinished borrows. The
   primary safety graph has no housekeeping escape from a protocol deadlock. *)
LiveInit ==
  /\ generation = 1 /\ incarnation = 1
  /\ alive = TRUE /\ owner = FALSE /\ allocated = TRUE /\ closing = TRUE
  /\ phase = [u \in Users |-> "queued"] /\ pins = Users
  /\ capturedGeneration = [u \in Users |-> 1]
  /\ capturedIncarnation = [u \in Users |-> 1]
  /\ cancelled = {} /\ resultGeneration = [u \in Users |-> 0]
  /\ deliveredIncarnation = [u \in Users |-> 0]
  /\ heartbeat = FALSE

Housekeeping ==
  /\ heartbeat' = ~heartbeat
  /\ UNCHANGED <<generation, incarnation, alive, owner, closing, allocated,
                  phase, pins, capturedGeneration, capturedIncarnation, cancelled,
                  resultGeneration, deliveredIncarnation>>
LiveNext == (\E u \in Users : Start(u) \/ Service(u) \/ Drain(u))
            \/ Free \/ Housekeeping
            \/ (AllRetired /\ ~allocated /\ UNCHANGED vars)
LiveSpec == LiveInit /\ [][LiveNext]_vars /\ WF_vars(Free) /\
            \A u \in Users : WF_vars(Start(u)) /\ WF_vars(Service(u)) /\ WF_vars(Drain(u))
EventuallyRetired == <> (AllRetired /\ ~allocated)
=============================================================================
