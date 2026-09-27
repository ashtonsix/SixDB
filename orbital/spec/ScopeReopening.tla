---------------------------- MODULE ScopeReopening ----------------------------
EXTENDS Contracts, TxFixtures, Integers, JournalSchedule
CONSTANTS JournalNone, TxNone, Bug, RestartDestination, StaleTiming, RouteTarget
J == INSTANCE JournalKernel
D == INSTANCE DurableLog
T == INSTANCE TxKernel

(* Authored close/drain -> real source-authority replacement -> different
   logical owner -> new-map admission. The map adapter owns version validation;
   TxKernel's fixed-parameter transaction machinery remains unchanged. *)
Base == Params("chain","none",FALSE)
Old == [Base EXCEPT !.shards={1,2}, !.keys={1}, !.home=[k \in {1}|->1],
  !.writes=[t \in 1..4|->IF t=2 THEN {} ELSE {1}],
  !.reads=[t \in 1..4|->IF t=1 THEN {} ELSE {1}],
  !.parts=[t \in 1..4|->IF t=2 THEN {} ELSE {1}],
  !.program=[t \in 1..4|->IF t=1 THEN "put" ELSE IF t=2 THEN "read" ELSE "increment"],
  !.causal=[t \in 1..4|->IF t=2 THEN 12 ELSE 0],
  !.value=[t \in 1..4|->1], !.dependencies=[k \in {1}|->{1}],
  !.firstRead=[t \in 1..4|->1],!.secondRead=[t \in 1..4|->1], !.slow={}]
New == [Old EXCEPT !.home=[k \in {1}|->2], !.parts=[t \in 1..4|->IF t=2 THEN {} ELSE {2}],
  !.owner=[t \in 1..4|->2],!.mapVersion=2, !.bug=IF Bug="drop-floor" THEN "migration-drop-floor" ELSE "none"]
JP == [owners|->{1},actors|->{T!Fold(1)}, subscribers|->[o \in {1}|->{T!Fold(1)}],
 initialConfig|->[o \in {1}|->0],configs|->{0,1},
 members|->[c \in {0,1}|->IF c=0 THEN {"A","B","C"} ELSE {"D","E","F"}],
 owner|->[c \in {0,1}|->1],ballots|->{1},
 leaders|->[c \in {0,1}|->[b \in {1}|->IF c=0 THEN "A" ELSE "D"]],
 successors|->[c \in {0,1}|->IF c=0 THEN {1} ELSE {}],ingress|->[o \in {1}|->{"A"}],
 mode|->"correct",compaction|->FALSE,opaqueCertificates|->TRUE,
 abstractVoters|->{"B","C","D","E","F"},atomicVoters|->{}]
DP == [owners|->{2},actors|->{T!Fold(2)},subscribers|->[o \in {2}|->{T!Fold(2)}],initialConfig|->[o \in {2}|->0]]
(* Inert sum tag sorts heterogeneous command bodies in TLC. It changes no
   command identity or application field; providers preserve it as exact bytes. *)
Tagged(c) == [a |-> c.kind] @@ c
Wire(e) == [a |-> ToString(<<e.kind,IF e.kind="tx.fact" THEN e.body.kind ELSE "">>)] @@
 (IF e.kind="journal.submit" THEN [e EXCEPT !.body=Tagged(@)] ELSE e)
WireEvents(es) == {Wire(es[i]):i \in 1..Len(es)}
EmptyDestination == [tx|->[T!Init(New) EXCEPT !.mapOpen=FALSE],imported|->TxNone,
                     activeMap|->0,sourceSeal|->TxNone,rejected|->{}]
VARIABLE run
vars == <<run>>
Init == run=[source|->T!Init(Old),destination|->EmptyDestination,journal|->J!Init(JP),other|->D!Init(DP),
 network|->{},closed|->FALSE,fault|->FALSE,handoff|->FALSE,recoverySent|->FALSE,restored|->FALSE,
 sealed|->FALSE,activateSent|->FALSE,destinationReset|->FALSE,destinationRecovered|->FALSE,
 staleSent|->FALSE,staleSource|->TxNone,staleTarget|->TxNone,savedFloor|->0,savedVersions|->{},
 evidence|->{},capture|->TxNone]
NewAuthority == \E v \in JP.members[1]:Len(run.journal.learned[1][v].seq)>0
SourceClosed == ~run.source.mapOpen /\ \A t \in run.source.oldMapPlans:
 run.source.decision[t]#TxNone /\ (Old.parts[t]={} \/ run.source.ticket[t][1]="resolved")
DestinationReady == run.destination.activeMap=2 /\ run.destination.tx.mapOpen /\
  (~RestartDestination \/ run.destinationRecovered)
Envelope(payload) == [source|->1,target|->RouteTarget,oldMap|->1,newMap|->2,scopes|->{1},payload|->payload]
SealCommand(payload) == Tagged(Command(1,"scope-seal","scope.seal",[tx|->1,key|->0,data|->Envelope(payload)]))
ImportCommand(seal) == Tagged(Command(2,"scope-import","map-transfer",
 [tx|->3,key|->0,data|->seal.body.data.payload,seal|->seal]))
Activation(sealID) == Tagged(Command(2,"scope-activate","scope.activate",
 [tx|->3,key|->0,data|->[map|->2,source|->1,seal|->sealID,import|->ToString("scope-import")]]))
ActivateCommand(seal) == Activation(seal.id)
Submit(c,src) == Wire(Event(<<"submit",c.id>>,src,c.owner,"journal.submit",Tagged(c)))

(* The destination map adapter consumes exact chosen commands. Mismatched map
   enrollment folds a durable refusal through the same Tx kernel; opening a
   scope is a separate ordered record after the imported closure. *)
RefuseBegin(s,c,index) ==
 LET t==c.body.tx
 IN Transition("scope.refuse-begin",[s EXCEPT !.recorded=@ \cup {c.id},
  !.commands=@ @@ (c.id :> c),!.replies=@ @@ (c.id :> <<>>),
  !.journalIndex[c.owner]=index,!.cancelled[t]=TRUE,!.decision[t]="abort"],<<>>)
DestinationRecord(s,c,index) ==
 LET validImport==IF c.kind="map-transfer" THEN
       c.id=ToString("scope-import") /\ c.body.seal.kind="scope.seal" /\
       c.body.seal.owner=1 /\ c.body.seal.body.data.source=1 /\ c.body.seal.body.data.target=2 /\
       c.body.seal.body.data.oldMap=1 /\ c.body.seal.body.data.newMap=2 /\
       c.body.seal.body.data.scopes={1} /\ c.body.data=c.body.seal.body.data.payload ELSE FALSE
     rejectImport==c.kind="map-transfer" /\ ~validImport
     rejectBegin==c.kind="begin" /\ c.body.data.map#s.activeMap /\ Bug#"stale-begin"
     reject==c.kind="enroll" /\ c.body.data#s.activeMap /\ Bug#"stale-map"
     input==IF reject THEN [s.tx EXCEPT !.mapOpen=FALSE,!.oldMapPlans={}] ELSE s.tx
     step==IF rejectBegin THEN RefuseBegin(s.tx,c,index)
       ELSE IF rejectImport /\ Bug#"accept-wrong-target" THEN
       Transition("map.reject-import",[s.tx EXCEPT !.recorded=@ \cup {c.id},
         !.commands=@ @@ (c.id :> c),!.replies=@ @@ (c.id :> <<>>)],<<>>)
       ELSE CHOOSE tr \in T!CommitAndFold(New,input,c):TRUE
     tx==[step.next EXCEPT !.journalIndex[2]=index,
          !.mapOpen=IF reject THEN s.tx.mapOpen ELSE @,
          !.versions=IF validImport /\ Bug="drop-data" THEN {} ELSE @]
     validActivate==IF c.kind="scope.activate" THEN
       Bug="early-activate" \/ (s.imported=c.body.data.import /\ s.sourceSeal=c.body.data.seal /\
       c.body.data.map=2 /\ c.body.data.source=1) ELSE FALSE
 IN [next|->[tx|->IF validActivate THEN [tx EXCEPT !.mapOpen=TRUE] ELSE tx,
             imported|->IF validImport THEN c.id ELSE s.imported,
             sourceSeal|->IF validImport THEN c.body.seal.id ELSE s.sourceSeal,
             activeMap|->IF validActivate THEN 2 ELSE s.activeMap,
             rejected|->IF rejectImport THEN s.rejected \cup {c.id} ELSE s.rejected],emissions|->step.emissions]
RECURSIVE ReplayDestination(_,_,_)
ReplayDestination(s,commands,index) ==
 IF commands = <<>> THEN s
 ELSE LET step==DestinationRecord(s,Head(commands),index+1)
      IN ReplayDestination(step.next,Tail(commands),index+1)

SourceAllowed(tr) ==
 /\ tr.tag#"tx.submit.map-transfer"
 /\ \A e \in WireEvents(tr.emissions): e.kind="journal.submit" =>
      e.body.body.tx \in (IF 1 \in run.source.published THEN {2} ELSE {1})
SourceSteps == {Transition(tr.tag,[run EXCEPT !.source=tr.next,!.network=@ \cup WireEvents(tr.emissions)],<<>>):
 tr \in {x \in T!Actions(Old,run.source):SourceAllowed(x)}}
DestinationSteps == IF DestinationReady THEN
 {Transition(tr.tag,[run EXCEPT !.destination.tx=tr.next,!.network=@ \cup WireEvents(tr.emissions)],<<>>):
 tr \in {x \in T!Actions(New,run.destination.tx):
   \A e \in Elements(x.emissions):e.kind="journal.submit" => e.body.body.tx=3}}
 ELSE {}
JournalSteps == {Transition(tr.tag,[run EXCEPT !.journal=tr.next,!.network=@ \cup WireEvents(tr.emissions)],<<>>):tr \in J!Actions(JP,run.journal)}
OtherSteps == {Transition(tr.tag,[run EXCEPT !.other=tr.next,!.network=@ \cup WireEvents(tr.emissions)],<<>>):
 tr \in {x \in D!Actions(DP,run.other):Bug#"early-activate" \/
  ~(x.tag="log.choose" /\ run.destination.activeMap=0 /\
   \E c \in Elements(x.next.log[2]):c.kind="map-transfer")}}
SourceReceive(e) ==
 IF e.kind="journal.deliver" /\ e.body.command.kind="begin" /\
    (~run.source.mapOpen /\ e.body.command.body.tx \notin run.source.oldMapPlans) /\ Bug#"stale-begin"
 THEN {RefuseBegin(run.source,e.body.command,e.body.index)}
 ELSE T!Receive(Old,run.source,e)
InputSteps(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN IF e.dst=1 THEN
   {Transition("input." \o e.kind,[run EXCEPT !.journal=tr.next,!.network=(@ \ {e}) \cup WireEvents(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE {Transition("input." \o e.kind,[run EXCEPT !.other=tr.next,!.network=(@ \ {e}) \cup WireEvents(tr.emissions)],<<>>):tr \in D!Receive(DP,run.other,e)}
 ELSE IF e.kind \in {"journal.deliver","journal.snapshot"}
 THEN IF e.body.owner=1 THEN
  {LET seal==e.kind="journal.deliver" /\ e.body.command.kind="scope.seal"
       extra==IF seal THEN {Submit(ImportCommand(e.body.command),T!Fold(1))} \cup
          (IF Bug="early-activate" THEN {Submit(ActivateCommand(e.body.command),T!Fold(1))} ELSE {}) ELSE {}
   IN Transition("input." \o e.kind,[run EXCEPT !.source=tr.next,
      !.restored=@ \/ e.kind="journal.snapshot",!.network=(@ \ {e}) \cup WireEvents(tr.emissions) \cup extra,
      !.evidence=@ \cup {e}],<<>>):tr \in SourceReceive(e)}
 ELSE IF e.kind="journal.snapshot" THEN
   {Transition("input.journal.snapshot",[run EXCEPT
      !.destination=ReplayDestination(EmptyDestination,e.body.prefix,0),!.destinationRecovered=TRUE,
      !.network=@ \ {e},!.evidence=@ \cup {e}],<<>>)}
 ELSE IF e.body.index=run.destination.tx.journalIndex[2]+1 THEN
   LET step==DestinationRecord(run.destination,e.body.command,e.body.index)
   IN {Transition("input.journal.deliver",[run EXCEPT !.destination=step.next,
       !.network=(@ \ {e}) \cup WireEvents(step.emissions),!.evidence=@ \cup {e}],<<>>)} ELSE {}
 ELSE IF e.kind="tx.fact" THEN
   IF e.body.tx=4 /\ e.body.kind="enroll" THEN
    {Transition("input.tx.fact",[run EXCEPT !.network=@ \ {e},
       !.staleSource=IF e.src=T!Fold(1) THEN e.body.value ELSE @,
       !.staleTarget=IF e.src=T!Fold(2) THEN e.body.value ELSE @],<<>>)}
   ELSE IF e.body.tx=3 THEN
    {Transition("input.tx.fact",[run EXCEPT !.destination.tx=tr.next,
       !.network=(@ \ {e}) \cup WireEvents(tr.emissions)],<<>>):tr \in T!Receive(New,run.destination.tx,e)}
   ELSE {Transition("input.tx.fact",[run EXCEPT !.source=tr.next,
       !.network=(@ \ {e}) \cup WireEvents(tr.emissions)],<<>>):tr \in T!Receive(Old,run.source,e)}
 ELSE IF e.kind="tx.published" THEN {Transition("input.published",[run EXCEPT !.network=@ \ {e}],<<>>)} ELSE {}
Inputs == UNION {InputSteps(e):e \in run.network}
Close ==
 /\ ~run.closed /\ {1,2} \subseteq run.source.published
 /\ LET tr==T!Request(Old,run.source,1,"map-close",0,1,TRUE)
    IN run'=[run EXCEPT !.source=tr.next,!.closed=TRUE,!.network=@ \cup WireEvents(tr.emissions)]
Fault ==
 /\ run.closed /\ SourceClosed /\ ~run.fault
 /\ run'=[run EXCEPT !.source=T!CrashFold(Old,run.source,1),!.fault=TRUE,
           !.savedFloor=run.source.bounds[1],!.savedVersions=run.source.versions]
Handoff ==
 /\ run.fault /\ ~run.handoff
 /\ LET e==Event("handoff","operator",1,"journal.handoff.request",[cfg|->0,target|->1])
    IN \E tr \in J!Receive(JP,run.journal,e):run'=[run EXCEPT !.journal=tr.next,!.handoff=TRUE]
Recover ==
 /\ run.fault /\ NewAuthority /\ ~run.recoverySent
 /\ LET e==Event("scope-recover",T!Fold(1),1,"journal.recover",[reason|->"replacement"])
    IN \E tr \in J!Receive(JP,run.journal,e):run'=[run EXCEPT !.journal=tr.next,!.recoverySent=TRUE]
Seal ==
 /\ run.restored /\ SourceClosed /\ ~run.sealed
 /\ LET c==SealCommand(T!ScopedTransfer(Old,run.source,1))
    IN run'=[run EXCEPT !.sealed=TRUE,!.capture=c,!.network=@ \cup {Submit(c,T!Fold(1))}]
Activate ==
 /\ ~run.activateSent /\ Bug#"early-activate" /\ run.destination.imported#TxNone
 /\ run'=[run EXCEPT !.activateSent=TRUE,!.network=@ \cup {Submit(Activation(run.destination.sourceSeal),T!Fold(2))}]
ResetDestination ==
 /\ RestartDestination /\ ~run.destinationReset /\ run.destination.activeMap=2
 /\ LET e==Event("destination-recover",T!Fold(2),2,"journal.recover",[reason|->"new-owner-reset"])
    IN run'=[run EXCEPT !.destination=EmptyDestination,!.destinationReset=TRUE,!.network=@ \cup {e}]
StaleReady == ~run.staleSent /\ (IF StaleTiming="before" THEN run.restored ELSE DestinationReady)
Stale ==
 /\ StaleReady
 /\ LET a==Command(1,"stale-source","enroll",[tx|->4,key|->1,data|->1])
        b==Command(2,"stale-target","enroll",[tx|->4,key|->2,data|->1])
        c==Command(1,"stale-source-begin","begin",[tx|->4,key|->0,data|->[profile|->T!ExecutionProfile(Old,4),map|->1]])
        d==Command(2,"stale-target-begin","begin",[tx|->4,key|->0,data|->[profile|->T!ExecutionProfile(New,4),map|->1]])
    IN run'=[run EXCEPT !.staleSent=TRUE,!.network=@ \cup
       {Submit(a,T!Driver(4)),Submit(b,T!Driver(4)),Submit(c,T!Driver(4)),Submit(d,T!Driver(4))}]
Candidates == {tr \in SourceSteps \cup DestinationSteps \cup JournalSteps \cup OtherSteps \cup Inputs:tr.next#run}
Mandatory == (~run.closed /\ {1,2} \subseteq run.source.published) \/
 (run.closed /\ SourceClosed /\ ~run.fault) \/ (run.fault /\ ~run.handoff) \/
 (run.fault /\ NewAuthority /\ ~run.recoverySent) \/
 (run.restored /\ SourceClosed /\ ~run.sealed) \/
 (RestartDestination /\ ~run.destinationReset /\ run.destination.activeMap=2) \/
 StaleReady \/ (~run.activateSent /\ Bug#"early-activate" /\ run.destination.imported#TxNone)
Ordinary == /\ ~Mandatory /\ Candidates#{}
            /\ LET tr==Choose(Candidates) IN run'=tr.next
Done == IF RouteTarget=2 THEN DestinationReady /\ 3 \in run.destination.tx.published /\
 run.staleSource#TxNone /\ run.staleTarget#TxNone /\
 ToString("stale-source-begin") \in run.source.recorded /\
 ToString("stale-target-begin") \in run.destination.tx.recorded ELSE run.destination.rejected#{}
Next == Close \/ Fault \/ Handoff \/ Recover \/ Seal \/ Activate \/ ResetDestination \/ Stale \/ Ordinary \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)
Completes == <>Done
JournalAgreement == \A x,y \in run.journal.chosenHistory:Comparable(x.seq,y.seq)
JournalEvidence == \A c \in {0,1}:\A v \in JP.members[c]:
 LET q==run.journal.learned[c][v] IN Len(q.seq)=0 \/ J!Quorum(JP,run.journal.acceptedHistory,q.cfg,q.ballot,q.seq)
SourceRestored == run.restored => ~run.source.mapOpen /\ run.source.bounds[1]>=run.savedFloor /\
 run.savedVersions \subseteq run.source.versions
ImportProvenance == run.destination.imported#TxNone =>
 \E e \in run.evidence:e.kind="journal.deliver" /\ e.body.owner=1 /\ e.body.command=run.capture /\
 \E h \in run.journal.chosenHistory:run.capture \in Elements(h.seq)
ActivationEvidence == run.destination.activeMap=2 => run.destination.imported#TxNone /\
 run.destination.sourceSeal#TxNone /\ \E c \in Elements(run.other.log[2]):c.kind="scope.activate"
ImportedClosure == run.destination.imported#TxNone =>
 run.destination.tx.bounds[1]>=run.savedFloor /\ run.savedVersions \subseteq run.destination.tx.versions
StaleBeginFence == 4 \notin run.source.begun /\ 4 \notin run.destination.tx.begun /\
 run.source.position[4]=0 /\ run.destination.tx.position[4]=0
StaleFence == run.source.enrolled[4]={} /\ run.destination.tx.enrolled[4]={} /\
 (run.staleSource=TxNone \/ run.staleSource=FALSE) /\ (run.staleTarget=TxNone \/ run.staleTarget=FALSE)
RejectedUntouched == RouteTarget#2 /\ run.destination.rejected#{} =>
 run.destination.tx.bounds[1]=0 /\ run.destination.tx.versions={} /\ run.destination.activeMap=0
NewOrdering == run.destination.tx.position[3]>0 => run.destination.tx.position[3]>run.savedFloor
NewObservation == run.destination.tx.readResult[3][1]#TxNone => run.destination.tx.readResult[3][1]=1
NewOutcome == 3 \in run.destination.tx.published =>
 run.destination.tx.decision[3]="commit" /\ run.destination.tx.outcome[3].result=2 /\
 \E v \in run.destination.tx.versions:v.tx=3 /\ v.key=1 /\ v.value=2
NoReopening == ~(Done /\ run.savedFloor>0 /\ run.destination.tx.outcome[3].result=2)
=============================================================================
