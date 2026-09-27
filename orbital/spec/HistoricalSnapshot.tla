---------------------------- MODULE HistoricalSnapshot ----------------------------
EXTENDS Contracts, TxFixtures, Integers
CONSTANTS TxNone, Bug, Missing, Requirement
K == INSTANCE TxKernel
R == INSTANCE CutMaterialKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
RequestedCut == 2
P == [Params("readonly-check","none",FALSE) EXCEPT !.checked={},!.material=TRUE,
 !.writes[1]={1},!.parts[1]={1},!.value[1]=7,!.causal[2]=Requirement]
RP == [shards |-> P.shards,transactions |-> P.transactions,keys |-> P.keys,home |-> P.home,
 initial |-> [k \in P.keys |-> 0],gc |-> Missing,bad |-> IF Bug="latest-material" THEN "latest-substitution" ELSE "none"]
JP == [owners |-> P.shards,actors |-> {K!Fold(a):a \in P.shards},
 subscribers |-> [a \in P.shards |-> {K!Fold(a)}],initialConfig |-> [a \in P.shards |-> 1]]
VARIABLES tx,material,journal,network,client
vars == <<tx,material,journal,network,client>>
Init == /\ tx=K!Init(P) /\ material=R!Init(RP) /\ journal=J!Init(JP) /\ network={} /\ client={}
RECURSIVE Facts(_,_)
Facts(s,es) == IF es= <<>> THEN s ELSE
 IF Head(es).kind="tx.fact" THEN Facts((CHOOSE tr \in K!Receive(P,s,Head(es)):TRUE).next,Tail(es))
 ELSE Facts(s,Tail(es))
Wire(e) == [a |-> ToString(<<e.kind,IF e.kind="tx.fact" THEN e.body.kind ELSE "">>)] @@
 (IF e.kind="journal.submit" THEN [e EXCEPT !.body=[a |-> @.kind] @@ @] ELSE e)
Forward(es) == {Wire(es[i]):i \in {j \in 1..Len(es):es[j].kind \notin {"tx.fact","root.source","root.retain-cut"}}}
CanRequest == 1 \in tx.published /\ (~Missing \/ R!Token(1,0) \in material.deleted)
PositionChoice(tr) == tr.tag="tx.submit.position" /\ Head(tr.emissions).body.body.tx=2
(* An explicit candidate readonly entry policy. The caller selects a retained
   historical cut subject to its actual freshness/causality requirement. The
   source's ordinary fresh-snapshot reply is not an output-reservation minimum:
   this reader has no output scopes. All later K ordering/execution is unchanged. *)
Adapt(tr) == IF ~PositionChoice(tr) THEN {tr} ELSE
 IF RequestedCut<Requirement /\ Bug#"downgrade"
 THEN IF K!CID(2,"cancel",0) \in tx.sent THEN {} ELSE
      {K!Request(P,tx,2,"cancel",0,P.owner[2],"requested snapshot precedes required causality/freshness")}
 ELSE {K!Request(P,tx,2,"position",0,P.owner[2],
        IF Bug="upgrade-cut" THEN Head(tr.emissions).body.body.data ELSE RequestedCut)}
Choices == UNION {Adapt(tr):tr \in {a \in K!Actions(P,tx):
 a.tag#"tx.submit.begin" \/ Head(a.emissions).body.body.tx#2 \/ CanRequest}}
KernelStep == \E tr \in Choices:
 /\ tx'=Facts(tr.next,tr.emissions) /\ material'=R!ObserveAll(RP,material,tr.emissions)
 /\ network'=network \cup Forward(tr.emissions) /\ UNCHANGED <<journal,client>>
JournalStep == \E tr \in J!Actions(JP,journal):
 /\ journal'=tr.next /\ network'=network \cup Forward(tr.emissions)
 /\ UNCHANGED <<tx,material,client>>
MaterialStep == \E tr \in R!Actions(RP,material):
 /\ material'=tr.next /\ network'=network \cup Forward(tr.emissions)
 /\ UNCHANGED <<tx,journal,client>>
Deliver == \E e \in network:
 \/ /\ e.kind="journal.submit"
    /\ \E tr \in J!Receive(JP,journal,e):journal'=tr.next /\ network'=(network \ {e}) \cup Forward(tr.emissions)
    /\ UNCHANGED <<tx,material,client>>
 \/ /\ e.kind \in {"journal.deliver","root.cut-protected","root.recipe-ready","root.unavailable"}
    /\ \E tr \in K!Receive(P,tx,e):
       /\ tx'=Facts(tr.next,tr.emissions) /\ material'=R!ObserveAll(RP,material,tr.emissions)
       /\ network'=(network \ {e}) \cup Forward(tr.emissions)
    /\ UNCHANGED <<journal,client>>
 \/ /\ e.kind="tx.published"
    /\ client'=client \cup {e.body} /\ network'=network \ {e}
    /\ UNCHANGED <<tx,material,journal>>
Done == \E result \in client:result.tx=2
Next == KernelStep \/ JournalStep \/ MaterialStep \/ Deliver \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(KernelStep) /\ WF_vars(JournalStep) /\ WF_vars(MaterialStep) /\ WF_vars(Deliver)
Completes == <>Done
RequestedCutPreserved == tx.position[2]>0 => tx.position[2]=RequestedCut
RequestedFreshness == tx.position[2]>0 => tx.position[2]>=Requirement
ExactMaterial == R!Exact(RP,material)
RetainedCoverage == R!CoverageRetained(RP,material)
IndependentMeaning == O!ObservedAtPosition(P,tx) /\ O!ResultSemantics(P,tx) /\ O!OutcomeJustified(P,tx) /\
 O!PublishedSemantics(P,tx)
HistoricalResult == Done =>
 IF RequestedCut<Requirement THEN tx.cancelled[2] /\ tx.position[2]=0 /\ tx.decision[2]="abort"
 ELSE IF Missing THEN tx.decision[2]="abort" /\ tx.outcome[2].status="unavailable" /\ tx.position[2]=RequestedCut
 ELSE tx.decision[2]="commit" /\ tx.outcome[2].result=0 /\ tx.position[1]>tx.position[2]
NoHistoricalResult == ~(Done /\ tx.decision[2]="commit" /\ tx.outcome[2].result=0 /\ tx.outcome[1].result=7)
NoExactFailure == ~(Done /\ tx.decision[2]="abort" /\ tx.position[2]=RequestedCut /\ material.failed#{})
=============================================================================
