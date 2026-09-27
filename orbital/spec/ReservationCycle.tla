------------------------- MODULE ReservationCycle -------------------------
EXTENDS Naturals, FiniteSets, Sequences
CONSTANTS Policy, Independent
K == INSTANCE ReservationKernel
Broad == 0
Narrow == {1,2}
Writes == [t \in 0..3 |-> CASE t=0 -> {1,2} [] t=1 -> {1} [] t=2 -> {2} [] OTHER -> {3}]
Start == LET empty==IF Independent THEN K!Fold(Policy,Writes,K!Initial,"enqueue",3).next ELSE K!Initial
             x==K!Fold(Policy,Writes,empty,"enqueue",1).next
             y==K!Fold(Policy,Writes,x,"enqueue",2).next
         IN K!Fold(Policy,Writes,y,"enqueue",Broad).next
VARIABLES state,finished,ticks
vars == <<state,finished,ticks>>
Init == /\ state=Start /\ finished={} /\ ticks=[t \in Narrow |-> FALSE]
Fix(t) == /\ t \in state.held
          /\ state'=K!Fold(Policy,Writes,state,"fix",t).next
          /\ finished'=finished \cup {t}
          /\ ticks'=IF t \in Narrow THEN [ticks EXCEPT ![t]=~@] ELSE ticks
Arrive(t) == /\ t \notin K!Items(state.order)
             /\ state'=K!Fold(Policy,Writes,state,"enqueue",t).next
             /\ UNCHANGED <<finished,ticks>>
Next == (\E t \in {Broad} \cup Narrow:Fix(t)) \/ (\E t \in Narrow:Arrive(t))
Spec == Init /\ [][Next]_vars
 /\ (\A t \in {Broad} \cup Narrow:WF_vars(Fix(t)))
 /\ (\A t \in Narrow:WF_vars(Arrive(t)))
Safety == K!Sound(Writes,state)
Closed == K!Eligible(Policy,Writes,state)={}
BroadProgress == <> (Broad \in finished)
NarrowProgress == \A t \in Narrow:([]<>ticks[t]) /\ ([]<>~ticks[t])
OldUnrelatedHeld == Independent => 3 \in state.held
NoBroad == Broad \notin finished
(* Narrow slots denote fresh local requests only after actual local fix. No
   network, command ID, full Tx state, or late packet is recycled by this model.
   Infinite arrivals/fixes are real enabled actions, not fairness on broad grant. *)
=============================================================================
