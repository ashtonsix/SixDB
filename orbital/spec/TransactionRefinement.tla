------------------------- MODULE TransactionRefinement -------------------------
EXTENDS Contracts, TxFixtures
CONSTANT TxNone
K == INSTANCE TxKernel
J == INSTANCE DurableLog
Base == Params("pair","none",FALSE)
P == [Base EXCEPT !.shards={1}, !.home=[k \in Base.keys |-> 1], !.writes[2]={}, !.program[2]="read", !.parts=[t \in Base.transactions |-> IF t=1 THEN {1} ELSE {}]]
JP == [owners |-> P.shards,actors |-> {K!Fold(a):a \in P.shards},
       subscribers |-> [a \in P.shards |-> {K!Fold(a)}],initialConfig |-> [a \in P.shards |-> 1]]
VARIABLES concrete, eager, journal, network, matched
vars == <<concrete,eager,journal,network,matched>>
RECURSIVE EagerFacts(_,_)
EagerFacts(st,es) == IF es= <<>> THEN st
  ELSE IF Head(es).kind="tx.fact"
       THEN EagerFacts((CHOOSE v \in K!Receive(P,st,Head(es)):TRUE).next,Tail(es))
       ELSE EagerFacts(st,Tail(es))
Init == /\ concrete=K!Init(P) /\ eager=K!Init(P) /\ journal=J!Init(JP)
        /\ network={} /\ matched=TRUE
Projection(s) == [s EXCEPT !.known=[t \in P.transactions |-> [k \in {} |-> TxNone]],
                         !.inputs=[t \in P.transactions |-> [k \in P.keys |-> TxNone]]]

KernelStep == \E tr \in K!Actions(P,concrete):
  LET matches=={a \in K!Actions(P,eager):a.tag=tr.tag /\ a.emissions=tr.emissions /\ Projection(a.next)=Projection(tr.next)}
  IN /\ concrete'=tr.next /\ network'=network \cup Elements(tr.emissions)
     /\ matched'=(matched /\ matches#{})
     /\ eager'=IF matches={} THEN eager ELSE EagerFacts((CHOOSE a \in matches:TRUE).next,tr.emissions)
     /\ UNCHANGED journal
JournalStep == \E tr \in J!Actions(JP,journal):
  /\ journal'=tr.next /\ network'=network \cup Elements(tr.emissions)
  /\ UNCHANGED <<concrete,eager,matched>>
InputStep == \E e \in network:
  \/ /\ e.kind="journal.submit"
     /\ \E tr \in J!Receive(JP,journal,e):
          /\ journal'=tr.next /\ network'=network \ {e}
          /\ UNCHANGED <<concrete,eager,matched>>
  \/ /\ e.kind="journal.deliver"
     /\ \E tr \in K!Receive(P,concrete,e):
          LET other==K!Receive(P,eager,e)
          IN /\ concrete'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
             /\ matched'=(matched /\ other#{})
             /\ eager'=IF other={} THEN eager ELSE EagerFacts((CHOOSE a \in other:TRUE).next,tr.emissions)
             /\ UNCHANGED journal
  \/ /\ e.kind="tx.fact"
     /\ \E tr \in K!Receive(P,concrete,e):
          /\ concrete'=tr.next /\ network'=network \ {e}
          /\ UNCHANGED <<eager,journal,matched>>
  \/ /\ e.kind="tx.published"
     /\ network'=network \ {e} /\ UNCHANGED <<concrete,eager,journal,matched>>
Done == concrete.published=P.transactions /\ network={}
Next == KernelStep \/ JournalStep \/ InputStep \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
(* Every concrete observable prefix has a matching eager prefix. The erased
   difference is only delivery of positive facts, never a command, read, decision
   or publication. Recovery, timers and absence-dependent cancellation are not
   in this quotient's precondition. *)
Refinement == matched /\ Projection(concrete)=Projection(eager) /\
   \A t \in P.transactions:(DOMAIN concrete.known[t] \subseteq DOMAIN eager.known[t] /\
     \A k \in DOMAIN concrete.known[t]:concrete.known[t][k]=eager.known[t][k])
NoCompletion == ~Done
=============================================================================
