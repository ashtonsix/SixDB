---------------------------- MODULE TransactionEpoch ----------------------------
EXTENDS Contracts, TxFixtures
CONSTANTS TxNone, Bug, LastEpoch, Mismatch, Schedule
K == INSTANCE TxKernel
C == INSTANCE TxProgram
O == INSTANCE TransactionOracle
Base == Params("basic",Bug,FALSE)
P == [Base EXCEPT !.checked={1}, !.externalChecks=TRUE,
      !.writes[3]={4}, !.reads[3]={}, !.program[3]="put", !.parts[3]={1},
      !.transactions=IF Schedule="reverse" THEN {1,2} ELSE @]
VARIABLES state, network, programs, stage, epoch
vars == <<state,network,programs,stage,epoch>>
Init == /\ state=K!Init(P) /\ network={}
        /\ programs=[r \in {1,2} |-> [t \in P.transactions |-> TxNone]]
        /\ stage=[r \in {1,2} |-> [t \in P.transactions |-> "cold"]]
        /\ epoch=1
RECURSIVE Facts(_,_)
Facts(s,es) == IF es= <<>> THEN s ELSE
  IF Head(es).kind="tx.fact"
  THEN Facts((CHOOSE tr \in K!Receive(P,s,Head(es)):TRUE).next,Tail(es))
  ELSE Facts(s,Tail(es))
Forward(es) == {e \in Elements(es):e.kind#"tx.fact"}
EventWeight(e) ==
  LET t==IF e.kind="journal.submit" THEN e.body.body.tx ELSE e.body.tx
      k==IF e.kind="journal.submit" THEN e.body.body.key ELSE IF e.kind="tx.fact" THEN e.body.key ELSE 0
  IN 100*(IF Schedule="reverse" THEN 4-t ELSE t)+k
Weight(tr) == IF tr.emissions= <<>> THEN 0 ELSE EventWeight(Head(tr.emissions))
KernelChoices == {v \in K!Actions(P,state):v.tag#"tx.compute"}
SelectedKernel == IF Schedule="all" THEN KernelChoices ELSE
  IF KernelChoices={} THEN {} ELSE
    {CHOOSE a \in KernelChoices: \A b \in KernelChoices:Weight(a)<=Weight(b)}
SelectedInputs == IF Schedule="all" THEN network ELSE
  IF network={} THEN {} ELSE {CHOOSE a \in network: \A b \in network:EventWeight(a)<=EventWeight(b)}
(* Joined-history families deliberately drain protocol service in authored
   forward/reverse transaction order. Each selected operation is an actual K
   transition. Both consumers' capture/page/compute stages remain independently
   scheduled. The unrestricted protocol product is the separate T family. *)
KernelStep ==
  /\ (Schedule="all" \/ network={})
  /\ \E tr \in SelectedKernel:
       /\ state'=Facts(tr.next,tr.emissions) /\ network'=network \cup Forward(tr.emissions)
       /\ UNCHANGED <<programs,stage,epoch>>
InputStep == \E e \in SelectedInputs:
  \/ /\ e.kind="journal.submit"
     /\ \E tr \in K!CommitAndFold(P,state,e.body):
          /\ state'=Facts(tr.next,tr.emissions) /\ network'=(network \ {e}) \cup Forward(tr.emissions)
          /\ UNCHANGED <<programs,stage,epoch>>
  \/ /\ e.kind="tx.published"
     /\ network'=network \ {e} /\ UNCHANGED <<state,programs,stage,epoch>>
Start(r,t) ==
  /\ programs[r][t]=TxNone
  /\ K!ExecutionReady(P,state,t)
  /\ programs'=[programs EXCEPT ![r][t]=C!Init(P,t,state.inputs[t],K!Context(P,state,t))]
  /\ stage'=[stage EXCEPT ![r][t]="captured"]
  /\ UNCHANGED <<state,network,epoch>>
Materialize(r,t) ==
  /\ stage[r][t]="captured"
  /\ stage'=[stage EXCEPT ![r][t]="ready"] /\ UNCHANGED <<state,network,epoch,programs>>
Execute(r,t) ==
  /\ stage[r][t]="ready" /\ programs[r][t].todo# <<>>
  /\ programs'=[programs EXCEPT ![r][t]=C!Step(P,t,@)]
  /\ UNCHANGED <<state,network,epoch,stage>>
CapturePrivate(t) ==
  /\ state.private[t]=TxNone /\ programs[1][t]#TxNone /\ programs[1][t].todo= <<>>
  /\ state'=K!CaptureExecution(P,state,t,C!Outcome(programs[1][t]))
  /\ UNCHANGED <<network,epoch,programs,stage>>
Check(r,t) ==
  /\ t \in P.checked /\ epoch=LastEpoch
  /\ programs[r][t]#TxNone /\ programs[r][t].todo= <<>>
  /\ K!CID(t,"report",r) \notin state.sent
  /\ LET report==[context |-> K!Context(P,state,t),request |-> IF Mismatch /\ r=2 THEN "b" ELSE "a",
                   outcome |-> C!Outcome(programs[r][t])]
         tr==K!Request(P,state,t,"report",r,P.owner[t],report)
     IN /\ state'=tr.next /\ network'=network \cup Elements(tr.emissions)
  /\ UNCHANGED <<epoch,programs,stage>>
Advance == /\ epoch<LastEpoch /\ epoch'=epoch+1 /\ UNCHANGED <<state,network,programs,stage>>
Done == state.published=P.transactions
Next == KernelStep \/ InputStep \/ Advance \/
        (\E t \in P.transactions:CapturePrivate(t) \/
          (\E r \in {1,2}:Start(r,t) \/ Materialize(r,t) \/ Execute(r,t) \/ Check(r,t))) \/
        (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(KernelStep) /\ WF_vars(InputStep) /\ WF_vars(Advance) /\
  \A t \in P.transactions:
    /\ WF_vars(CapturePrivate(t))
    /\ \A r \in {1,2}:WF_vars(Start(r,t)) /\ WF_vars(Materialize(r,t)) /\ WF_vars(Execute(r,t)) /\ WF_vars(Check(r,t))
Completes == <>Done
SameProgram == \A t \in P.transactions:
  programs[1][t]#TxNone /\ programs[2][t]#TxNone /\
  programs[1][t].todo= <<>> /\ programs[2][t].todo= <<>> => programs[1][t]=programs[2][t]
SemanticRefinement == O!ObservedAtPosition(P,state) /\ O!ResultSemantics(P,state) /\
                      O!OutcomeJustified(P,state) /\ O!PublishedSemantics(P,state)
VerifiedPublication == 1 \in state.published /\ state.decision[1]="commit" =>
  state.reports[1][1]#TxNone /\ state.reports[1][1]=state.reports[1][2]
ExactContext == \A r \in {1,2}: \A t \in P.transactions:programs[r][t]#TxNone =>
  programs[r][t].context=K!Context(P,state,t)
NoUnrelatedProgress == ~(3 \in state.published /\ state.decision[1]=TxNone /\ epoch=1)
NoCompletion == ~Done
=============================================================================
