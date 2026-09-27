----------------------------- MODULE DerivedDelivery -----------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS TxNone, Bug, LateJoin
T == INSTANCE TxKernel
C == INSTANCE TxProgram
D == INSTANCE DeliveryKernel
J == INSTANCE DurableLog
S == INSTANCE JournalSchedule
O == INSTANCE TransactionOracle
SourceReset == Bug \in {"source-reset","source-reset-identity"}
TP == [Params("single","none",SourceReset) EXCEPT !.checked={1},!.reads[1]={1},
      !.program[1]="increment",!.externalCompute={1},!.externalChecks=TRUE]
Recipients == IF LateJoin THEN {4,5,6} ELSE {4,5}
DActor(a) == <<"delivery",a>>
ColdContext == <<1,0,TP.profile,TP.mapVersion>>
DP(context,cut) ==
 [keys |-> {1},baseKeys |-> {1},child |-> 2,parent |-> 1,origins |-> {1,2},
  recipients |-> Recipients,initialMembers |-> {4,5},
  inputs |-> [o \in {1,2} |-> [k \in {1} |-> 0]],expected |-> [k \in {1} |-> 1],
  lineage |-> "lineage",call |-> "result",cut |-> cut,interpretation |-> "canonical",
  identities |-> [k \in {1} |-> ToString(<<context,"result",0>>)],context |-> context,
  forms |-> {"raw","folded"},routes |-> 2,duplicates |-> 2,dynamic |-> FALSE,
  cancelRecipients |-> {},reset |-> TRUE,cancel |-> FALSE,join |-> LateJoin,
  bad |-> IF Bug \in {"route-identity","wrong-transform","no-app-dedup","transport-completion","split-checkpoint"} THEN Bug ELSE "none",
  owner |-> 3,externalOrigins |-> TRUE]
Owners == {1,2,3} \cup Recipients
JP == [owners |-> Owners,actors |-> {T!Fold(1),T!Fold(2)} \cup {DActor(a):a \in {3} \cup Recipients},
 subscribers |-> [o \in Owners |-> IF o \in {1,2} THEN {T!Fold(o)} ELSE {DActor(o)}],
 initialConfig |-> [o \in Owners |-> 1]]
VARIABLE run
vars == <<run>>
P == DP(run.context,run.cut)
Init == run=[tx |-> T!Init(TP),delivery |-> D!Init(DP(ColdContext,0)),journal |-> J!Init(JP),
 network |-> {},programs |-> [r \in {1,2} |-> C!Init(TP,1,[k \in TP.reads[1] |-> 0],ColdContext)],started |-> {},dispatched |-> FALSE,
 mainSent |-> FALSE,reportsSent |-> {},originsSent |-> {},originCommit |-> {},
 context |-> ColdContext,cut |-> 0,published |-> TxNone,originEvents |-> {},sourceCrash |-> TxNone,sourceSnapshots |-> {}]
Dispatch ==
 IF ~run.dispatched /\ T!ExecutionReady(TP,run.tx,1)
 THEN {Transition("application.dispatch",[run EXCEPT !.dispatched=TRUE,
       !.network=@ \cup {Event(<<"execution",r>>,T!Driver(1),r,"execution.start",
         [inputs |-> run.tx.inputs[1],context |-> T!Context(TP,run.tx,1),cut |-> run.tx.position[1]]):r \in {1,2}}],<<>>)} ELSE {}
TxChoices == {tr \in T!Actions(TP,run.tx):tr.tag#"tx.driver-crash"}
TxSteps == {Transition(tr.tag,[run EXCEPT !.tx=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):
             tr \in TxChoices}
Execute(r) ==
 /\ r \in run.started /\ run.programs[r].todo# <<>>
 /\ LET next==C!Step(TP,1,run.programs[r])
        divergent==Bug="origin-mismatch" /\ r=2 /\ Head(run.programs[r].todo).op="wasm-add"
    IN run'=[run EXCEPT !.programs[r]=IF divergent THEN [next EXCEPT !.result=@+1] ELSE next]
MainResult ==
 IF ~run.mainSent /\ 1 \in run.started /\ run.programs[1].todo= <<>>
 THEN {Transition("execution.main-result",[run EXCEPT !.mainSent=TRUE,
       !.network=@ \cup {Event("main-result",1,T!Driver(1),"execution.result",
        [tx |-> 1,context |-> run.programs[1].context,cut |-> run.cut,
         outcome |-> C!Outcome(run.programs[1])])}],<<>>)} ELSE {}
Report(r) ==
 IF r \notin run.reportsSent /\ r \in run.started /\ run.programs[r].todo= <<>>
 THEN {Transition("execution.report",[run EXCEPT !.reportsSent=@ \cup {r},
       !.network=@ \cup {Event(<<"report",r>>,r,1,"journal.submit",
         Command(1,T!CID(1,"report",r),"report",[tx |-> 1,key |-> r,
           data |-> [context |-> run.programs[r].context,request |-> "query-1",
                     outcome |-> C!Outcome(run.programs[r])]]))}],<<>>)} ELSE {}
Origin(r) ==
 IF r \notin run.originsSent /\ r \in run.started /\ run.programs[r].todo= <<>> /\
    (r \in run.originCommit \/ Bug="publish-tentative")
 THEN LET output==Head(run.programs[r].outbox)
          event==Event(<<"origin",r>>,r,3,"delivery.derived",
             [origin |-> r,key |-> 1,id |-> IF Bug="source-reset-identity" /\ run.tx.incarnation[1]>0 THEN ToString(<<output.id,run.tx.incarnation[1]>>) ELSE ToString(output.id),value |-> output.value,
              context |-> run.programs[r].context])
      IN {Transition("execution.outbox",[run EXCEPT !.originsSent=@ \cup {r},
            !.network=@ \cup {event},!.originEvents=@ \cup {event}],<<>>)} ELSE {}
Kind(tr) == IF tr.emissions= <<>> THEN "" ELSE
 IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE ""
DAllowed(tr) ==
 /\ (tr.tag#"route-replacement" \/ run.delivery.transport#{})
 /\ (tr.tag#"recipient-process-reset" \/
       (tr.next.restarted \ run.delivery.restarted={4} /\
        DOMAIN run.delivery.custody[4]#{} /\ 4 \notin run.delivery.done[1]))
 /\ (Kind(tr)#"delivery.complete" \/ Head(tr.emissions).body.owner#4 \/
       4 \in run.delivery.restarted)
 /\ (Kind(tr)#"delivery.ack" \/ run.delivery.route=2)
 /\ (Kind(tr)#"delivery.join" \/ 1 \in run.delivery.closed)
DFault(tr) == tr.tag \in {"route-replacement","recipient-process-reset"} \/ Kind(tr)="delivery.join"
DChoices == {tr \in D!Actions(P,run.delivery):DAllowed(tr)}
DSteps == {Transition(tr.tag,[run EXCEPT !.delivery=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in {x \in DChoices:~DFault(x)}}
FaultSteps == {Transition(tr.tag,[run EXCEPT !.delivery=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in {x \in DChoices:DFault(x)}}
JournalSteps == {Transition(tr.tag,[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in J!Actions(JP,run.journal)}
Input(e) ==
 IF e.kind="execution.start"
 THEN {Transition("input.execution",[run EXCEPT !.programs[e.dst]=C!Init(TP,1,[k \in TP.reads[1] |-> e.body.inputs[k]],e.body.context),!.started=@ \cup {e.dst},
       !.context=e.body.context,!.cut=e.body.cut,!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="origin.commit"
 THEN {Transition("input.origin-commit",[run EXCEPT !.originCommit=@ \cup {e.dst},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input." \o e.kind,[run EXCEPT !.journal=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,IF e.kind="journal.recover" /\ e.dst \in Recipients THEN [e EXCEPT !.src=DActor(e.dst)] ELSE e)}
 ELSE IF e.kind="tx.published"
 THEN {Transition("input.published",[run EXCEPT !.published=e.body,!.network=(@ \ {e}) \cup
       (IF e.body.decision="commit" THEN {Event(<<"commit",r>>,"client",r,"origin.commit",e.body):r \in {1,2}} ELSE {})],<<>>)}
 ELSE IF e.kind \in {"tx.fact","execution.result"} \/
              (e.kind \in {"journal.deliver","journal.snapshot"} /\ e.body.owner \in {1,2})
 THEN {Transition("input." \o e.kind,[run EXCEPT !.tx=tr.next,
       !.sourceSnapshots=IF e.kind="journal.snapshot" THEN @ \cup {e.body} ELSE @,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in T!Receive(TP,run.tx,e)}
 ELSE IF e.kind="delivery.payload" /\ e.dst=5 /\ e.body.form="raw" /\
              D!Logical(P,1) \notin DOMAIN run.delivery.custody[5]
 THEN {}
 ELSE {Transition("input." \o e.kind,[run EXCEPT !.delivery=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in D!Receive(P,run.delivery,e)}
\* Ordinary service drains real messages and journal callbacks before issuing
\* more work. Select a local transition before forming the product successor;
\* sets of complete product states make TLC compare enormous unrelated values.
EligibleInputs == {e \in run.network:Input(e)#{}}
BothComputed == run.started={1,2} /\ \A r \in {1,2}:run.programs[r].todo= <<>>
AppSteps == LET readyOrigins==UNION {Origin(r):r \in {1,2}} IN
 Dispatch \cup (IF BothComputed THEN
   IF readyOrigins#{} THEN readyOrigins
   ELSE MainResult \cup UNION {Report(r):r \in {1,2}} ELSE {})
Service ==
 IF EligibleInputs#{} THEN
   LET e==CHOOSE e \in EligibleInputs:TRUE
       tr==CHOOSE tr \in Input(e):TRUE IN run'=tr.next
 ELSE IF J!Actions(JP,run.journal)#{} THEN
   LET tr==CHOOSE tr \in J!Actions(JP,run.journal):TRUE
   IN run'=[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF TxChoices#{} THEN
   LET tr==CHOOSE tr \in TxChoices:TRUE
   IN run'=[run EXCEPT !.tx=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF AppSteps#{} THEN
   LET tr==CHOOSE tr \in AppSteps:TRUE IN run'=tr.next
 ELSE LET normal=={tr \in DChoices:~DFault(tr)} IN
   /\ normal#{}
   /\ LET tr==CHOOSE tr \in normal:TRUE
      IN run'=[run EXCEPT !.delivery=tr.next,!.network=@ \cup Elements(tr.emissions)]
Fault == \E tr \in {x \in DChoices:DFault(x)}:
 run'=[run EXCEPT !.delivery=tr.next,!.network=@ \cup Elements(tr.emissions)]
SourceCrash ==
 /\ SourceReset /\ run.sourceCrash=TxNone /\ run.network={}
 /\ run.tx.outcome[1]#TxNone /\ run.tx.decision[1]=TxNone
 /\ \E tr \in T!CrashActions(TP,run.tx):
  run'=[run EXCEPT !.tx=tr.next,!.sourceCrash=[outcome|->run.tx.outcome[1],cut|->run.tx.position[1]],
        !.started={},!.dispatched=FALSE,!.mainSent=FALSE,!.reportsSent={},
        !.programs=[r \in {1,2}|->C!Init(TP,1,[k \in TP.reads[1]|->0],ColdContext)]]
DeliveryDone == /\ 1 \in run.delivery.closed /\ run.originsSent={1,2}
                /\ run.delivery.route=2 /\ 4 \in run.delivery.restarted
                /\ (~LateJoin \/ 6 \in run.delivery.joinedComplete)
Done == run.published#TxNone /\
        (IF run.published.decision="commit" THEN DeliveryDone ELSE run.originEvents={})
Next == IF ENABLED SourceCrash THEN SourceCrash ELSE
 Service \/ Fault \/ (\E r \in {1,2}:Execute(r)) \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(SourceCrash) /\ WF_vars(Service) /\ WF_vars(Fault) /\ \A r \in {1,2}:WF_vars(Execute(r))
Completes == <>Done
OnlyCommitted == \A e \in run.originEvents:
 /\ run.tx.decision[1]="commit"
 /\ [id |-> <<e.body.context,"result",0>>,value |-> e.body.value] \in Elements(run.tx.outcome[1].outbox)
IndependentOrigins == \A a,b \in run.originEvents:a.body.id=b.body.id => a.body.value=b.body.value /\ a.body.context=b.body.context
SerialReads == O!ObservedAtPosition(TP,run.tx)
SerialOutcome == O!ResultSemantics(TP,run.tx)
FailureJustified == O!OutcomeJustified(TP,run.tx)
EffectMultiplicity == D!EffectMultiplicity(P,run.delivery)
Coverage == D!Coverage(P,run.delivery)
Canonical == D!Canonical(P,run.delivery)
Checkpoints == D!Checkpoints(P,run.delivery)
JoinedCoverage == D!JoinedCoverage(P,run.delivery)
NoContentConflict == run.delivery.conflicts={}
\* In this application fixture key1 is the source progress cell. Its increment
\* and the emitted contribution are fields of one actual outcome command.
SourceCheckpointOutbox == \A e \in run.originEvents:run.tx.decision[1]="commit" =>
 /\ e.body.id=ToString(<<e.body.context,"result",0>>)
 /\ e.body.context[2]=run.tx.position[1]
 /\ \E c \in Elements(run.journal.log[1]):
   /\ c.kind="outcome" /\ c.body.data.context=e.body.context
   /\ c.body.data.effects[1]=e.body.value
   /\ [id|-><<e.body.context,"result",0>>,value|->e.body.value] \in Elements(c.body.data.outbox)
RecoveredSource == run.sourceCrash#TxNone /\ run.originEvents#{} =>
 /\ run.tx.position[1]=run.sourceCrash.cut /\ run.tx.outcome[1]=run.sourceCrash.outcome
 /\ \E b \in run.sourceSnapshots:b.owner=1 /\ Prefix(b.prefix,run.journal.log[1]) /\
       \E c \in Elements(b.prefix):c.kind="outcome" /\ c.body.data=run.sourceCrash.outcome
NoSourceReplacement == ~(Done /\ run.sourceCrash#TxNone /\ run.tx.incarnation[1]=1 /\ run.sourceSnapshots#{})
NoCompletion == ~Done
NoMismatchAbort == ~(run.published#TxNone /\ run.published.decision="abort" /\ run.originEvents={})
NoC3Seam == ~(Done /\ run.originsSent={1,2} /\
 (\E a,b \in run.delivery.arrivals:a.id=b.id /\ a.recipient=b.recipient /\ a.route=1 /\ b.route=2) /\
 (\E c \in run.delivery.custodyEvidence:c.recipient=5 /\ c.carriers#{} /\ \A a \in c.carriers:a.form="folded") /\
 (\E cut \in run.delivery.restartCuts:cut.recipient=4 /\ cut.custody#{}) /\
 run.delivery.replays#{})
NoLateJoin == ~(Done /\ 6 \in run.delivery.joinedComplete /\
                \E j \in run.delivery.joinHistory:1 \in j.closed)
=============================================================================
