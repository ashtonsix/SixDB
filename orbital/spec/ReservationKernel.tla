------------------------- MODULE ReservationKernel -------------------------
EXTENDS Naturals, FiniteSets, Sequences

(* Candidate queue-policy operator. This state is the live local reservation
   projection: order retains original enqueue order through queued/held/announced,
   and removes a request only at locally durable fix or cancellation. It contains
   no clocks, remote state, transaction values, decisions, or eventual-grant oracle.
   A future TxKernel adapter can project requestOrder/ticket into this exact API.
   Record eligibility (e.g. a fix has a chosen immutable c) belongs to its caller. *)
Items(q) == {q[i]:i \in 1..Len(q)}
At(q,t) == CHOOSE i \in 1..Len(q):q[i]=t
Remove(q,t) == SelectSeq(q,LAMBDA u:u#t)
Initial == [order |-> <<>>,held |-> {}]
Conflict(writes,t,u) == writes[t] \cap writes[u]#{}
Older(s,u,t) == At(s.order,u)<At(s.order,t)
Waiting(s) == Items(s.order) \ s.held
Barrier(policy,writes,s,w) ==
 CASE policy="ordered" -> TRUE
 [] policy="eligible" -> FALSE
 [] policy="drain" -> ~\E u \in Items(s.order):Older(s,u,w) /\ Conflict(writes,u,w)
 [] policy="head" -> w=Head(s.order)

CanGrant(policy,writes,s,t) ==
 /\ t \in Waiting(s)
 /\ ~\E h \in s.held:Conflict(writes,t,h)
 /\ ~\E w \in Waiting(s):Older(s,w,t) /\ Conflict(writes,w,t) /\ Barrier(policy,writes,s,w)
Eligible(policy,writes,s) == {t \in Waiting(s):CanGrant(policy,writes,s,t)}
First(policy,writes,s) == CHOOSE t \in Eligible(policy,writes,s):
 \A u \in Eligible(policy,writes,s):At(s.order,t)<=At(s.order,u)
Event(kind,t,writes) == [kind |-> kind,tx |-> t,scopes |-> writes[t]]
Grant(writes,s,t) == [next |-> [s EXCEPT !.held=@ \cup {t}],
                     outputs |-> <<Event("grant",t,writes)>>]
RECURSIVE Close(_,_,_)
Close(policy,writes,s) ==
 IF Eligible(policy,writes,s)={} THEN [next |-> s,outputs |-> <<>>]
 ELSE LET g==Grant(writes,s,First(policy,writes,s))
          rest==Close(policy,writes,g.next)
      IN [next |-> rest.next,outputs |-> g.outputs \o rest.outputs]
Raw(s,kind,t) ==
 IF kind="enqueue" THEN [s EXCEPT !.order=Append(@,t)]
 ELSE [s EXCEPT !.order=Remove(@,t),!.held=@ \ {t}]
Fold(policy,writes,s,kind,t) ==
 LET raw==Raw(s,kind,t) closed==Close(policy,writes,raw)
 IN [next |-> closed.next,
     outputs |-> <<Event(kind,t,writes)>> \o closed.outputs]

Sound(writes,s) ==
 /\ Len(s.order)=Cardinality(Items(s.order))
 /\ s.held \subseteq Items(s.order)
 /\ \A t,u \in s.held:t#u => ~Conflict(writes,t,u)
=============================================================================
