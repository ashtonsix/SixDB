------------------------------ MODULE RecoveryCut ------------------------------
EXTENDS Contracts, TxFixtures, Integers
CONSTANTS TxNone, Bug, Mode
RestoreActor == <<"restore",0>>
T == INSTANCE TxKernel
C == INSTANCE RecoveryCutKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
P == Params("pair","none",FALSE)
CP == [owners |-> {0,1,2},bug |-> Bug]
JP == [owners |-> {0,1,2},actors |-> {RestoreActor,T!Fold(1),T!Fold(2)},
       subscribers |-> [a \in {0,1,2} |-> IF a=0 THEN {RestoreActor} ELSE {T!Fold(a)}],
       initialConfig |-> [a \in {0,1,2} |-> 1]]
VARIABLES tx,cut,journal,network,phase,restored,externalDead,oldTried
vars == <<tx,cut,journal,network,phase,restored,externalDead,oldTried>>
Init == /\ tx=T!Init(P) /\ cut=C!Init(CP) /\ journal=J!Init(JP) /\ network={}
        /\ phase=0 /\ restored=TxNone /\ externalDead=FALSE /\ oldTried=FALSE
Max(xs) == CHOOSE x \in xs: \A y \in xs:x>=y
Image(s) == [k \in {1,2} |->
  LET vs=={v \in s.versions:v.key=k}
  IN IF vs={} THEN 0 ELSE (CHOOSE v \in vs:v.position=Max({w.position:w \in vs})).value]
Flatten(prefix) == prefix[1] \o prefix[2]
Partial == 1 \in tx.resolved[1] /\ 1 \notin tx.resolved[2]
TxChoices == IF phase \notin {0,2} THEN {} ELSE
  LET t==IF 1 \in tx.published THEN 2 ELSE 1
  IN T!SubmitActions(P,tx,t) \cup T!ReadActions(P,tx,t) \cup T!ComputeActions(P,tx,t) \cup
     T!PublishActions(P,tx,t) \cup UNION {{T!Grant(P,tx,t,a):a \in {b \in P.shards:T!CanGrant(P,tx,t,b)}}}
TxStep ==
  /\ ~(phase=0 /\ Partial)
  /\ \E tr \in {v \in TxChoices:phase#0 \/ v.tag#"tx.submit.install" \/ Head(v.emissions).body.body.key=1}:
       /\ tx'=tr.next /\ network'=network \cup Elements(tr.emissions)
       /\ UNCHANGED <<cut,journal,phase,restored,externalDead,oldTried>>
Offer == /\ phase=0 /\ Partial /\ network={}
  /\ LET tr==CHOOSE v \in C!Receive(CP,cut,Event("offer","local-fold",RestoreActor,"cut.offer",
         [prefix |-> [a \in {1,2}|->journal.log[a]],image |-> Image(tx)])):TRUE
     IN /\ cut'=tr.next /\ network'=network \cup Elements(tr.emissions)
  /\ phase'=1 /\ UNCHANGED <<tx,journal,restored,externalDead,oldTried>>
Continue == /\ phase=1 /\ cut.pointer#TxNone /\ network={}
  /\ phase'=2 /\ UNCHANGED <<tx,cut,journal,network,restored,externalDead,oldTried>>
Disaster == /\ phase=2 /\ tx.published=P.transactions /\ network={}
  /\ LET tr==CHOOSE v \in C!Receive(CP,cut,Event(RestoreActor,"operator",RestoreActor,"cut.restore",
              [mode |-> Mode,cut |-> tx.position[1]])):TRUE
     IN /\ cut'=tr.next /\ network'=Elements(tr.emissions)
  /\ phase'=3 /\ UNCHANGED <<tx,journal,restored,externalDead,oldTried>>
HeadState == T!ReplayCommands(P,T!Init(P),Flatten(cut.heads)).next
ClosedIds == {t \in P.transactions:HeadState.position[t]<=cut.cut /\ HeadState.position[t]>0 /\ HeadState.decision[t]#TxNone}
ClosedCommands == SelectSeq(Flatten(cut.heads),LAMBDA c:
  IF c.kind="journal.barrier" THEN FALSE ELSE c.body.tx \in ClosedIds)
NormalState ==
  LET base==T!ReplayCommands(P,T!Init(P),Flatten(C!Base(cut)[1])).next
      suffix==SuffixAfter(cut.heads[1],cut.pointer.cursor[1]) \o SuffixAfter(cut.heads[2],cut.pointer.cursor[2])
  IN T!ReplayCommands(P,base,suffix).next
ClosedState == T!ReplayCommands(P,T!Init(P),ClosedCommands).next
Abandoned == UNION {Elements(HeadState.outcome[t].outbox):
  t \in {u \in P.transactions:u \notin ClosedIds /\ HeadState.outcome[u]#TxNone /\ HeadState.decision[u]="commit"}}
Rebuild == /\ phase=3 /\ cut.rebuilt=TxNone /\ C!MaterialReady(cut) /\ C!HeadsReady(CP,cut)
  /\ LET rebuilt==IF Mode="normal" THEN NormalState ELSE ClosedState
         image==IF Bug="torn-cut" /\ Mode="pitr" THEN C!Base(cut)[2] ELSE Image(rebuilt)
         body==[mode |-> Mode,lineage |-> IF Mode="normal" THEN 1 ELSE 2,cut |-> cut.cut,
                 image |-> image,abandoned |-> IF Mode="pitr" THEN Abandoned ELSE {},
                 historical |-> UNION {Elements(HeadState.outcome[t].outbox):
                    t \in {u \in P.transactions:HeadState.decision[u]="commit" /\ (Mode="normal" \/ u \in ClosedIds)}}]
         tr==CHOOSE v \in C!Receive(CP,cut,Event("rebuilt","engine",RestoreActor,"cut.rebuilt",body)):TRUE
     IN /\ cut'=tr.next /\ restored'=rebuilt
  /\ UNCHANGED <<tx,journal,network,phase,externalDead,oldTried>>
ExternalFence == /\ Mode="pitr" /\ phase=3 /\ ~externalDead
  /\ externalDead'=TRUE
  /\ network'=network \cup {Event("fenced","deployment",RestoreActor,"deployment.fenced",[lineage |-> 1])}
  /\ UNCHANGED <<tx,cut,journal,phase,restored,oldTried>>
OldWrite == /\ cut.opened /\ ~oldTried
  /\ oldTried'=TRUE
  /\ network'=network \cup {Event("old-write","old-executor",RestoreActor,"old.write",
        [lineage |-> IF Mode="normal" THEN 0 ELSE 1,image |-> <<9,9>>])}
  /\ UNCHANGED <<tx,cut,journal,phase,restored,externalDead>>
CutStep == \E tr \in C!Actions(CP,cut):
  /\ cut'=tr.next /\ network'=network \cup Elements(tr.emissions)
  /\ UNCHANGED <<tx,journal,phase,restored,externalDead,oldTried>>
JournalStep == \E tr \in J!Actions(JP,journal):
  /\ journal'=tr.next /\ network'=network \cup Elements(tr.emissions)
  /\ UNCHANGED <<tx,cut,phase,restored,externalDead,oldTried>>
Deliver == \E e \in network:
 \/ /\ e.kind \in {"journal.submit","journal.recover"}
    /\ \E tr \in J!Receive(JP,journal,e):
         /\ journal'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
         /\ UNCHANGED <<tx,cut,phase,restored,externalDead,oldTried>>
 \/ /\ e.kind \in {"journal.deliver","journal.snapshot","deployment.fenced","old.write"} /\ ToString(e.dst)=ToString(RestoreActor)
    /\ \E tr \in C!Receive(CP,cut,e):
         /\ cut'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
         /\ UNCHANGED <<tx,journal,phase,restored,externalDead,oldTried>>
 \/ /\ e.kind \in {"journal.deliver","tx.fact"} /\ ToString(e.dst)#ToString(RestoreActor)
    /\ \E tr \in T!Receive(P,tx,e):
         /\ tx'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
         /\ UNCHANGED <<cut,journal,phase,restored,externalDead,oldTried>>
 \/ /\ e.kind \in {"tx.published","cut.opened","external.intent"}
    /\ network'=network \ {e} /\ UNCHANGED <<tx,cut,journal,phase,restored,externalDead,oldTried>>
Local == TxStep \/ Offer \/ Continue \/ Disaster \/ Rebuild \/ ExternalFence \/ OldWrite \/ CutStep
Service == Deliver \/ (~ENABLED Deliver /\ JournalStep) \/
           (~ENABLED Deliver /\ ~ENABLED JournalStep /\ Local)
Done == cut.opened /\ oldTried /\ network={}
Next == Service \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Service)
Completes == <>Done
CheckpointDurable == C!PublishedDurable(cut)
CursorExact == C!CursorExact(cut)
NormalRestoration == Mode="normal" /\ cut.opened =>
  /\ cut.image=Image(tx) /\ restored.bounds=tx.bounds /\ restored.position=tx.position
  /\ restored.fixed=tx.fixed /\ restored.decision=tx.decision /\ restored.versions=tx.versions
ExpectedCutImage == [k \in {1,2} |->
  LET prior=={t \in P.transactions:tx.position[t]<=cut.cut /\ tx.decision[t]="commit" /\ k \in DOMAIN tx.outcome[t].effects}
  IN IF prior={} THEN 0 ELSE tx.outcome[CHOOSE t \in prior:tx.position[t]=Max({tx.position[u]:u \in prior})].effects[k]]
ClosedCut == Mode="pitr" /\ cut.opened => cut.image=ExpectedCutImage
PhysicalFence == Mode="pitr" /\ cut.opened => externalDead /\ cut.fence#TxNone
ExpectedAbandonment == UNION {Elements(tx.outcome[t].outbox):
  t \in {u \in P.transactions:tx.position[u]>cut.cut /\ tx.decision[u]="commit"}}
Abandonment == Mode="pitr" /\ cut.fence#TxNone =>
  {o.id:o \in cut.fence.abandoned}={o.id:o \in ExpectedAbandonment}
NoHistoricalEmission == cut.opened => ~\E e \in network:e.kind \in {"tx.published","external.intent"}
LiveSemantics == O!ObservedAtPosition(P,tx) /\ O!ResultSemantics(P,tx)
NoPartialCheckpoint == ~(cut.pointer#TxNone /\ C!MaterialReady(cut) /\ C!Base(cut)[2]= <<1,0>>)
NoRestoration == ~Done
=============================================================================
