------------------------- MODULE ReservationAcquisition -------------------------
EXTENDS Naturals, FiniteSets, Sequences
CONSTANTS Policy, WrongOrder, AllowCancel, Wide
K == INSTANCE ReservationKernel
Tx == IF Wide THEN {0,1,2} ELSE {0,1}
Shards == {1,2}
Traffic == IF Wide THEN 3 ELSE 2
Writes == [t \in Tx \cup {Traffic} |-> IF t=Traffic THEN {3}
 ELSE IF ~Wide THEN {1} ELSE CASE t=0 -> {1} [] t=1 -> {1,2} [] OTHER -> {2}]
Order(t) == IF WrongOrder /\ t=1 THEN <<2,1>> ELSE <<1,2>>
Pair(t,s) == [tx |-> t,shard |-> s]
Record(kind,t,s) == [kind |-> kind,tx |-> t,shard |-> s]
Pairs == {Pair(t,s):t \in Tx,s \in Shards}
GrantPairs(es,s) == {Pair(e.tx,s):e \in {g \in K!Items(es):g.kind="grant" /\ g.tx \in Tx}}
VARIABLES queues,records,grants,issued,known,position,cancelled,tombstones,fixed,finished,tick
vars == <<queues,records,grants,issued,known,position,cancelled,tombstones,fixed,finished,tick>>
Init == /\ queues=[s \in Shards |-> K!Initial]
 /\ records={} /\ grants={} /\ issued={} /\ known={} /\ position={}
 /\ cancelled={} /\ tombstones={} /\ fixed={} /\ finished={} /\ tick=FALSE
Submit(t,s) ==
 /\ t \notin position \cup cancelled /\ Pair(t,s) \notin issued
 /\ (s=Order(t)[1] \/ Pair(t,Order(t)[1]) \in known)
 /\ issued'=issued \cup {Pair(t,s)}
 /\ records'=records \cup {Record("enqueue",t,s)}
 /\ UNCHANGED <<queues,grants,known,position,cancelled,tombstones,fixed,finished,tick>>
DeliverRecord(t,s,kind) ==
 /\ Record(kind,t,s) \in records
 /\ LET tr==IF kind="enqueue" /\ Pair(t,s) \in tombstones
             THEN [next |-> queues[s],outputs |-> <<>>]
             ELSE K!Fold(Policy,Writes,queues[s],kind,t)
    IN /\ queues'=[queues EXCEPT ![s]=tr.next]
       /\ grants'=grants \cup GrantPairs(tr.outputs,s)
 /\ records'=records \ {Record(kind,t,s)}
 /\ tombstones'=(IF kind="cancel" THEN tombstones \cup {Pair(t,s)} ELSE tombstones)
 /\ fixed'=(IF kind="fix" THEN fixed \cup {Pair(t,s)} ELSE fixed)
 /\ UNCHANGED <<issued,known,position,cancelled,finished,tick>>
ReceiveGrant(t,s) == /\ Pair(t,s) \in grants
 /\ known'=(IF t \in cancelled THEN known ELSE known \cup {Pair(t,s)})
 /\ grants'=grants \ {Pair(t,s)}
 /\ UNCHANGED <<queues,records,issued,position,cancelled,tombstones,fixed,finished,tick>>
ChoosePosition(t) == /\ t \notin position \cup cancelled
 /\ (\A s \in Shards:Pair(t,s) \in known)
 /\ position'=position \cup {t}
 /\ records'=records \cup {Record("fix",t,s):s \in Shards}
 /\ UNCHANGED <<queues,grants,issued,known,cancelled,tombstones,fixed,finished,tick>>
Cancel(t) == /\ AllowCancel /\ t \notin position \cup cancelled
 /\ (~Wide \/ t=1)
 /\ cancelled'=cancelled \cup {t}
 /\ records'=records \cup {Record("cancel",t,s):s \in Shards}
 /\ UNCHANGED <<queues,grants,issued,known,position,tombstones,fixed,finished,tick>>
Finish(t) == /\ t \notin finished
 /\ ((\A s \in Shards:Pair(t,s) \in fixed) \/ (\A s \in Shards:Pair(t,s) \in tombstones))
 /\ finished'=finished \cup {t}
 /\ UNCHANGED <<queues,records,grants,issued,known,position,cancelled,tombstones,fixed,tick>>
TrafficStep ==
 LET held==Traffic \in queues[1].held
     tr==K!Fold(Policy,Writes,queues[1],IF held THEN "fix" ELSE "enqueue",Traffic)
 IN /\ queues'=[queues EXCEPT ![1]=tr.next]
    /\ grants'=grants \cup GrantPairs(tr.outputs,1)
    /\ tick'=(IF held THEN ~tick ELSE tick)
 /\ UNCHANGED <<records,issued,known,position,cancelled,tombstones,fixed,finished>>
Next == TrafficStep \/ (\E t \in Tx:
 Cancel(t) \/ ChoosePosition(t) \/ Finish(t) \/
 (\E s \in Shards:Submit(t,s) \/ ReceiveGrant(t,s) \/
     (\E kind \in {"enqueue","fix","cancel"}:DeliverRecord(t,s,kind))))
Spec == Init /\ [][Next]_vars /\ WF_vars(TrafficStep)
 /\ (\A t \in Tx:WF_vars(ChoosePosition(t)) /\ WF_vars(Finish(t)) /\
    (\A s \in Shards:WF_vars(Submit(t,s)) /\ WF_vars(ReceiveGrant(t,s)) /\
      (\A kind \in {"enqueue","fix","cancel"}:WF_vars(DeliverRecord(t,s,kind)))))
Safety == \A s \in Shards:K!Sound(Writes,queues[s])
Closed == \A s \in Shards:K!Eligible(Policy,Writes,queues[s])={}
PositionEvidence == \A t \in position:\A s \in Shards:Pair(t,s) \in known
CancellationExclusive == position \cap cancelled={}
Retirement == \A p \in fixed \cup tombstones:p.tx \notin K!Items(queues[p.shard].order)
Ascending == ~WrongOrder => \A t \in Tx:Pair(t,2) \in issued => Pair(t,1) \in known
Completes == <> (finished=Tx)
IndependentProgress == ([]<>tick) /\ ([]<>~tick)
NoPartialHold == ~(\E t \in Tx:t \in queues[1].held /\ t \notin queues[2].held /\ Pair(t,2) \in issued)
NoCrossedCancel == ~(\E t \in cancelled:\E s \in Shards:
 Pair(t,s) \in tombstones /\ Record("enqueue",t,s) \in records)
NoCancelledCompletion == ~(finished=Tx /\ cancelled#{})
NoDistributedBypass == ~(Wide /\ 0 \in queues[1].held /\ Pair(0,2) \in issued /\
 1 \in K!Waiting(queues[1]) /\ 2 \in queues[1].held)
(* A chosen owner position and pre-position cancellation are disjoint abstract
   durable decisions. Each local chosen record folds and closes atomically;
   record delivery and grant delivery are independently delayable. Participants
   retain cancellation tombstones even if the reservation arrives afterwards.
   This is an acquisition/queue projection, not replicated journal or Tx value
   execution. Traffic denotes fresh disjoint local operations after actual fix;
   it carries no recycled packet or transaction identity across its cycles.
   Wide selects X narrow{1}, B broad{1,2}, C narrow{2} and independent traffic{3}.
   Its optional cancellation is the authored B cancellation only; the two-request
   family separately varies cancellation at both distributed request owners. *)
=============================================================================
