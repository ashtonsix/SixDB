------------------------ MODULE RetainedTransactions ------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS Scenario, Bug, TxNone
K == INSTANCE TxKernel
R == INSTANCE CutMaterialKernel
O == INSTANCE TransactionOracle
P == [Params(Scenario,"none",FALSE) EXCEPT !.material=TRUE,
 !.transactions=IF Scenario="late-source" THEN {1,3} ELSE @]
RP == [shards |-> P.shards,transactions |-> P.transactions,keys |-> P.keys,home |-> P.home,
 initial |-> [k \in P.keys |-> 0],gc |-> TRUE,bad |-> Bug]
VARIABLES state,material,network
vars == <<state,material,network>>
Init == /\ state=K!Init(P) /\ material=R!Init(RP) /\ network={}
RECURSIVE Facts(_,_)
Facts(st,es) == IF es= <<>> THEN st ELSE
 IF Head(es).kind="tx.fact"
 THEN LET steps == K!Receive(P,st,Head(es))
      IN Facts(IF steps={} THEN st ELSE (CHOOSE t \in steps:TRUE).next,Tail(es))
 ELSE Facts(st,Tail(es))
Forward(es) == {e \in Elements(es):e.kind \notin {"tx.fact","root.source","root.retain-cut"}}
KernelStep == \E t \in K!Actions(P,state):
 /\ state'=Facts(t.next,t.emissions) /\ material'=R!ObserveAll(RP,material,t.emissions)
 /\ network'=network \cup Forward(t.emissions)
FoldStep == \E e \in {m \in network:m.kind="journal.submit"}:
 \E t \in K!CommitAndFold(P,state,e.body):
 /\ state'=Facts(t.next,t.emissions) /\ material'=R!ObserveAll(RP,material,t.emissions)
 /\ network'=(network \ {e}) \cup Forward(t.emissions)
MaterialStep == \E t \in R!Actions(RP,material):
 /\ material'=t.next /\ UNCHANGED state /\ network'=network \cup Elements(t.emissions)
InputStep == \E e \in network:
 \/ /\ e.kind \in {"root.cut-protected","root.recipe-ready","root.unavailable","tx.fact"}
    /\ \E t \in K!Receive(P,state,e):
       /\ state'=Facts(t.next,t.emissions) /\ material'=R!ObserveAll(RP,material,t.emissions)
       /\ network'=(network \ {e}) \cup Forward(t.emissions)
 \/ /\ e.kind="tx.published" /\ network'=network \ {e} /\ UNCHANGED <<state,material>>
Done == state.published=P.transactions
Terminal == Done /\ UNCHANGED vars
Next == KernelStep \/ FoldStep \/ MaterialStep \/ InputStep \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(KernelStep) /\ WF_vars(FoldStep) /\ WF_vars(MaterialStep) /\ WF_vars(InputStep)
ExactMaterial == R!Exact(RP,material)
RetainedCoverage == R!CoverageRetained(RP,material)
SerialReads == O!ObservedAtPosition(P,state)
SerialOutcomes == O!ResultSemantics(P,state)
JustifiedOutcome == O!OutcomeJustified(P,state)
Publication == O!PublishedSemantics(P,state)
Completes == <>Done
NoMaterialRead == material.observations={}
NoPendingCut == ~(\E id \in DOMAIN material.closures:material.closures[id].pending#{})
NoLateFailure == material.failed={}
NoAgreedLateFailure == ~(\E t \in state.published:state.decision[t]="abort" /\ state.outcome[t].status="unavailable")
NoPendingCompletion == ~(Done /\
 \E o \in material.observations:
   R!Root(o.request) \in DOMAIN material.closures /\
   material.closures[R!Root(o.request)].pending#{})
=============================================================================
