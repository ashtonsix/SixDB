------------------------- MODULE Reservations -------------------------
EXTENDS Integers, Sequences, FiniteSets

(***************************************************************************
 A reservation/position leaf, not a second transaction implementation.
 Bridge fixture: W=1 writes {x@A,y@B}; B=2 writes {x@A,z@A}; C=3 writes {z@A}.
 It asks whether durable local fixing lets the next conflicting group acquire
 while W's remote fix remains undelivered, and exposes the W -> B -> C convoy.

 Each Request/Grant/Announce/Fix is an ordered durable shard command. All local
 scopes of a group are acquired atomically. Only the next canonical shard is
 requested; blocked requests do not reserve scopes on later shards. Fix copies
 the coordinator's immutable durable c, then releases only that shard's group.

 RemoteFloor imports a prior registered read boundary on B; it is not a global
 clock. With RemoteFloor=12, W can announce 5 on A but receive 17 from B. A bad
 early release on A then lets a successor choose 10 using only W's lower
 announcement. The independent grant-order/position invariant detects this
 substantive ordering error, beyond merely asserting release needs evidence.

 Queues are operational state. Request/grant orders are bounded observer
 projections of the agreed input (each of three transactions requests a shard
 once). They are never consulted to grant, announce, or fix. There is no state
 constraint, arbitrary clock cap, packet multiplicity, application interpreter,
 MVCC, computation, C2, failure/recovery or epoch-confluence claim.
 A second geometry gives W and B both {x@A,y@B}, leaving C on {z@A}.
 Reversing only B's acquisition order can then create an actual two-holder
 wait cycle, while the unrelated C can still complete. Neither finite shape
 proves starvation freedom under an indefinitely replenished workload.
***************************************************************************)
CONSTANTS Bad, Geometry, RemoteFloor
ASSUME Bad \in {"none", "early-release", "skip-waiter", "reverse-order"}
ASSUME Geometry \in {"bridge", "cycle"}
ASSUME RemoteFloor \in {0, 12}

Tx == {1, 2, 3}
Shards == {1, 2}
Keys == {1, 2, 3}
Home(k) == IF k = 2 THEN 2 ELSE 1
Writes(t) == CASE t = 1 -> {1, 2}
                 [] t = 2 -> IF Geometry = "bridge" THEN {1, 3} ELSE {1, 2}
                 [] OTHER -> {3}
Parts(t) == {Home(k) : k \in Writes(t)}
LocalWrites(t, s) == {k \in Writes(t) : Home(k) = s}
Conflict(t, u, s) == LocalWrites(t, s) \cap LocalWrites(u, s) # {}
Elements(q) == {q[i] : i \in 1..Len(q)}
At(q, t) == IF t \in Elements(q) THEN CHOOSE i \in 1..Len(q) : q[i] = t ELSE 0
Remove(q, t) == SelectSeq(q, LAMBDA u : u # t)
Max(S) == CHOOSE n \in S : \A m \in S : n >= m
Min(S) == CHOOSE n \in S : \A m \in S : n <= m
NextStamp(t, floor) == 4 * ((floor \div 4) + 1) + t

VARIABLES queue, acquired, released, minimum, position, localC,
          requestOrder, grantOrder
vars == <<queue, acquired, released, minimum, position, localC,
          requestOrder, grantOrder>>

Init ==
  /\ queue = [s \in Shards |-> <<>>]
  /\ acquired = [t \in Tx |-> {}]
  /\ released = [t \in Tx |-> {}]
  /\ minimum = [t \in Tx |-> [s \in Shards |-> 0]]
  /\ position = [t \in Tx |-> 0]
  /\ localC = [t \in Tx |-> [s \in Shards |-> 0]]
  /\ requestOrder = [s \in Shards |-> <<>>]
  /\ grantOrder = [s \in Shards |-> <<>>]

Holds(t, s) == s \in acquired[t] \ released[t]
Waiting(t) == {s \in Shards : t \in Elements(queue[s])}
NextShard(t) == IF Bad = "reverse-order" /\ t = 2
               THEN Max(Parts(t) \ acquired[t]) ELSE Min(Parts(t) \ acquired[t])

Request(t, s) ==
  /\ acquired[t] # Parts(t) /\ Waiting(t) = {}
  /\ s = NextShard(t)
  /\ queue' = [queue EXCEPT ![s] = Append(@, t)]
  /\ requestOrder' = [requestOrder EXCEPT ![s] = Append(@, t)]
  /\ UNCHANGED <<acquired, released, minimum, position, localC, grantOrder>>

HolderAllows(t, s) == \A u \in Tx \ {t} : Holds(u, s) => ~Conflict(t, u, s)
WaitersAllow(t, s) == \A i \in 1..(At(queue[s], t) - 1) : ~Conflict(t, queue[s][i], s)
Grant(t, s) ==
  /\ t \in Elements(queue[s]) /\ HolderAllows(t, s)
  /\ IF Bad = "skip-waiter"
     THEN \A i \in 1..(At(queue[s], t) - 1) : ~HolderAllows(queue[s][i], s)
     ELSE WaitersAllow(t, s)
  /\ queue' = [queue EXCEPT ![s] = Remove(@, t)]
  /\ acquired' = [acquired EXCEPT ![t] = @ \cup {s}]
  /\ grantOrder' = [grantOrder EXCEPT ![s] = Append(@, t)]
  /\ UNCHANGED <<released, minimum, position, localC, requestOrder>>

LocalFloor(t, s) ==
  Max({IF s = 2 THEN RemoteFloor ELSE 0} \cup
      {IF localC[u][s] > 0 THEN localC[u][s] ELSE minimum[u][s] :
         u \in {v \in Tx \ {t} : Conflict(t, v, s)}})
Announce(t, s) ==
  /\ s \in Parts(t) /\ acquired[t] = Parts(t) /\ minimum[t][s] = 0
  /\ minimum' = [minimum EXCEPT ![t][s] = NextStamp(t, LocalFloor(t, s))]
  /\ UNCHANGED <<queue, acquired, released, position, localC, requestOrder, grantOrder>>

(** Coalesced delivery of all actual minimum replies, then durable c. *)
ChoosePosition(t) ==
  /\ position[t] = 0 /\ \A s \in Parts(t) : minimum[t][s] > 0
  /\ position' = [position EXCEPT ![t] = Max({minimum[t][s] : s \in Parts(t)})]
  /\ UNCHANGED <<queue, acquired, released, minimum, localC, requestOrder, grantOrder>>

Fix(t, s) ==
  /\ s \in Parts(t) /\ position[t] > 0 /\ localC[t][s] = 0
  /\ localC' = [localC EXCEPT ![t][s] = position[t]]
  /\ released' = [released EXCEPT ![t] = @ \cup {s}]
  /\ UNCHANGED <<queue, acquired, minimum, position, requestOrder, grantOrder>>

EarlyRelease(t, s) ==
  /\ Bad = "early-release" /\ s \in Parts(t) /\ position[t] > 0 /\ Holds(t, s)
  /\ released' = [released EXCEPT ![t] = @ \cup {s}]
  /\ UNCHANGED <<queue, acquired, minimum, position, localC, requestOrder, grantOrder>>

Done == \A t \in Tx : \A s \in Parts(t) : localC[t][s] > 0
Terminal == Done /\ UNCHANGED vars
Next ==
  \/ \E t \in Tx, s \in Shards : Request(t, s) \/ Grant(t, s)
       \/ Announce(t, s) \/ Fix(t, s) \/ EarlyRelease(t, s)
  \/ \E t \in Tx : ChoosePosition(t)
  \/ Terminal
SafetySpec == Init /\ [][Next]_vars
Fairness ==
  /\ \A t \in Tx, s \in Shards :
       /\ WF_vars(Request(t, s)) /\ WF_vars(Grant(t, s))
       /\ WF_vars(Announce(t, s)) /\ WF_vars(Fix(t, s))
  /\ \A t \in Tx : WF_vars(ChoosePosition(t))
Spec == SafetySpec /\ Fairness
EventuallyFixed == <>Done

(** Independent checks; no protocol guard calls the observer histories. *)
TypeOK ==
  /\ queue \in [Shards -> Seq(Tx)] /\ requestOrder \in [Shards -> Seq(Tx)]
  /\ grantOrder \in [Shards -> Seq(Tx)]
  /\ acquired \in [Tx -> SUBSET Shards] /\ released \in [Tx -> SUBSET Shards]
  /\ minimum \in [Tx -> [Shards -> Nat]] /\ localC \in [Tx -> [Shards -> Nat]]
  /\ position \in [Tx -> Nat]
  /\ \A s \in Shards : Len(requestOrder[s]) <= 3 /\ Len(grantOrder[s]) <= 3
ExclusiveHolders == \A t, u \in Tx : \A s \in Shards :
  t # u /\ Holds(t, s) /\ Holds(u, s) => ~Conflict(t, u, s)
LocalFixHasPosition == \A t \in Tx : \A s \in Shards :
  localC[t][s] > 0 => localC[t][s] = position[t]
ReleaseHasEvidence == \A t \in Tx : \A s \in released[t] : localC[t][s] > 0
UniquePositions == \A t, u \in Tx :
  t # u /\ position[t] > 0 /\ position[u] > 0 => position[t] # position[u]
ConflictingPositionsFollowGrants == \A t, u \in Tx : \A s \in Shards :
  (At(grantOrder[s], t) > 0 /\ At(grantOrder[s], t) < At(grantOrder[s], u) /\
   Conflict(t, u, s) /\ position[t] > 0 /\ position[u] > 0) => position[t] < position[u]
NoOvertaking == \A t, u \in Tx : \A s \in Shards :
  (t \in Elements(queue[s]) /\ s \in acquired[u] /\
   At(requestOrder[s], t) < At(requestOrder[s], u)) => ~Conflict(t, u, s)
CanonicalAcquisition == \A t \in Tx : \A s \in acquired[t] \cup Waiting(t) :
  \A earlier \in Parts(t) : earlier < s => earlier \in acquired[t]

(** Dependency is computed independently from queue positions and holders.
    Three transactions need only two-/three-edge cycles; self-edges excluded. *)
WaitsFor(t, u) == t # u /\ \E s \in Shards :
  /\ t \in Elements(queue[s]) /\ Conflict(t, u, s)
  /\ Holds(u, s) \/ (u \in Elements(queue[s]) /\ At(queue[s], u) < At(queue[s], t))
NoReservationCycle ==
  /\ \A t, u \in Tx : ~(WaitsFor(t, u) /\ WaitsFor(u, t))
  /\ \A t, u, v \in Tx : ~(WaitsFor(t, u) /\ WaitsFor(u, v) /\ WaitsFor(v, t))

(** False invariants whose counterexamples establish the intended reachability. *)
NoLocalReleaseBenefit == ~(At(grantOrder[1], 1) > 0 /\
  At(grantOrder[1], 2) > At(grantOrder[1], 1) /\ localC[1][1] > 0 /\ localC[1][2] = 0)
NoIndirectConvoy == ~(Geometry = "bridge" /\ Holds(1, 1) /\
  2 \in Elements(queue[1]) /\ 3 \in Elements(queue[1]) /\
  At(queue[1], 2) < At(queue[1], 3))
=============================================================================
