------------------------- MODULE ReservationPolicies -------------------------
EXTENDS Contracts, Integers
CONSTANTS Policy, Scene, TxNone
K == INSTANCE TxKernel

\* This is a local reservation-policy projection, not a recycling of full
\* transaction identities. A narrow slot represents a fresh younger request
\* only after its predecessor has locally fixed/released. No delayed messages,
\* decisions or dedup records use the slot. The actual CanGrant predicate is
\* shared; the first eligible waiter matches the simulator's ordered scan.
Broad == 0
Narrow == {1,2}
Bridge == Scene="bridge"
Tx == IF Bridge THEN 0..4 ELSE 0..2
Writes == [t \in Tx |-> CASE t=0 -> {1,2}
 [] t=1 -> {1} [] t=2 -> {2} [] t=3 -> {3} [] OTHER -> {1}]
P == [transactions |-> Tx,writes |-> Writes,home |-> [k \in 1..3 |-> 1],
      bug |-> "none",queuePolicy |-> Policy]
VARIABLES queue,held,finished,ticks,order
vars == <<queue,held,finished,ticks,order>>
Init == /\ queue=IF Bridge THEN <<0,2,3,4>> ELSE <<0>>
        /\ held=IF Bridge THEN {1} ELSE Narrow
        /\ order=(IF Bridge THEN <<1>> ELSE <<1,2>>) \o queue
        /\ finished={} /\ ticks=[t \in Narrow |-> FALSE]
Projected == [foldUp |-> [a \in {1} |-> TRUE],queue |-> [a \in {1} |-> queue],
 requestOrder |-> [a \in {1} |-> order],
 ticket |-> [t \in Tx |-> [a \in {1} |-> IF t \in held THEN "held" ELSE IF t \in Elements(queue) THEN "queued" ELSE "none"]]]
Eligible == {t \in Elements(queue):K!CanGrant(P,Projected,t,1)}
First == CHOOSE t \in Eligible: \A u \in Eligible:K!At(queue,t)<=K!At(queue,u)
Grant == /\ Eligible#{}
         /\ held'=held \cup {First} /\ queue'=K!Remove(queue,First)
         /\ UNCHANGED <<finished,ticks,order>>
Release(t) == /\ t \in held /\ ~(Bridge /\ t=1)
              /\ held'=held \ {t}
              /\ order'=K!Remove(order,t)
              /\ finished'=finished \cup {t}
              /\ ticks'=IF t \in Narrow THEN [ticks EXCEPT ![t]=~@] ELSE ticks
              /\ UNCHANGED queue
Arrive(t) == /\ ~Bridge /\ t \in Narrow
             /\ t \notin held /\ t \notin Elements(queue)
             /\ queue'=Append(queue,t) /\ order'=Append(order,t)
             /\ UNCHANGED <<held,finished,ticks>>
Terminal == Bridge /\ Eligible={} /\ held={1} /\ UNCHANGED vars
Next == Grant \/ (\E t \in Tx:Release(t)) \/ (\E t \in Narrow:Arrive(t)) \/ Terminal
Spec == Init /\ [][Next]_vars /\ WF_vars(Grant)
        /\ (\A t \in Tx:WF_vars(Release(t)))
        /\ (\A t \in Narrow:WF_vars(Arrive(t)))

DisjointHolders == \A t,u \in held:t#u => Writes[t] \cap Writes[u]={}
NoDuplicateWaiter == Len(queue)=Cardinality(Elements(queue))
BroadProgress == <> (Broad \in finished)
NarrowKeepsCompleting == \A t \in Narrow:([]<>ticks[t]) /\ ([]<>~ticks[t])
IndependentProgress == <> (3 \in finished)
NeighbourProgress == <> (2 \in finished)
NoDirectBypass == Bridge => 4 \notin held \cup finished
NoWaiterBridgeBypass == (Bridge /\ Policy="ordered") => 2 \notin held \cup finished
NoBroadCompletion == Broad \notin finished
NoBridgeBypass == ~(2 \in finished /\ 1 \in held)
=============================================================================
