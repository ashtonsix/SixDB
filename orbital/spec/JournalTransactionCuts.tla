------------------------ MODULE JournalTransactionCuts ------------------------
EXTENDS Contracts, TxFixtures, Integers, JournalSchedule
CONSTANTS JournalNone, TxNone, Scenario, Topology, FaultCut, Bug
J == INSTANCE JournalKernel
D == INSTANCE DurableLog
T == INSTANCE TxKernel
O == INSTANCE TransactionOracle

\* Authored joined histories: ordinary service is deterministic between the
\* named causal cuts. Isolated J/T families cover their wider local schedules.
\* Every prefix here is created by real shared-kernel transitions; no prepared
\* state, completed record, or repaired application cache is injected.
Base == Params(IF Scenario="C1" THEN "pair" ELSE "three",Bug,Scenario="C1")
P == IF Scenario="C1" THEN [Base EXCEPT !.writes[2]={}, !.parts[2]={},
                                  !.program[2]="read", !.reads[2]={1}]
     ELSE [Base EXCEPT !.migration=TRUE, !.reads[3]={1},!.program[3]="read",!.causal[3]=7]
Members(c) == IF c=0 THEN {"A","B","C"}
              ELSE IF Topology="overlap" THEN {"B","C","D"} ELSE {"D","E","F"}
Leader(c,b) == IF c=0 THEN (IF b=1 THEN "A" ELSE "B")
               ELSE IF Topology="overlap" THEN (IF b=1 THEN "B" ELSE "C")
               ELSE IF b=1 THEN "D" ELSE "E"
JP == [owners |-> {1},actors |-> {T!Fold(1)},
       subscribers |-> [j \in {1}|->{T!Fold(1)}], initialConfig |-> [j \in {1}|->0],
       configs |-> {0,1},members |-> [c \in {0,1}|->Members(c)],
       owner |-> [c \in {0,1}|->1], ballots |-> {1,2},
       leaders |-> [c \in {0,1}|->[b \in {1,2}|->Leader(c,b)]],
       successors |-> [c \in {0,1}|->IF c=0 THEN {1} ELSE {}],
       ingress |-> [o \in {1}|->IF FaultCut="media-loss" THEN Members(0) ELSE {"A"}],
       mode |-> "correct",compaction |-> FALSE,opaqueCertificates |-> TRUE,
       abstractVoters |-> {"B","C","D","E","F"},atomicVoters |-> {}]
DP == [owners |-> {2},actors |-> {T!Fold(2)},
       subscribers |-> [j \in {2}|->{T!Fold(2)}],initialConfig |-> [j \in {2}|->0]]
VARIABLE run
vars == <<run>>
Init == run=[tx|->T!Init(P),journal|->J!Init(JP),other|->D!Init(DP),network|->{},
             fault|->FALSE,handoff|->FALSE,savedPosition|->0,savedBound|->0,
             cutVoters|->0,crossed|->FALSE,restored|->FALSE,recoveryRequested|->FALSE,savedMapPlans|->{}]

HasFix(seq) == \E c \in Elements(seq):c.kind=(IF FaultCut="decision" THEN "decision" ELSE "fix") /\ c.owner=1 /\ c.body.tx=1
FixDurable == \E r \in run.journal.acceptedHistory:
  r.cfg=0 /\ HasFix(r.seq) /\
    (IF FaultCut="one-accept" THEN TRUE ELSE J!Quorum(JP,run.journal.acceptedHistory,0,r.ballot,r.seq))
CutReady == IF Scenario="C1"
            THEN (IF FaultCut="decision" THEN run.tx.decision[1]=TxNone
                  ELSE run.tx.fixed[1][2]>0 /\ run.tx.fixed[1][1]=0) /\ FixDurable
            ELSE ~run.tx.mapOpen /\ T!Holds(run.tx,1,1) /\ ~T!Holds(run.tx,1,2)
NewAuthority == \E v \in Members(1):Len(run.journal.learned[1][v].seq)>0

TxAllowed(tr) ==
  /\ tr.tag#"tx.driver-crash"
  /\ IF Scenario="C1"
     THEN /\ (2 \in run.tx.published \/
            ~(\E e \in Elements(tr.emissions):e.kind="journal.submit" /\ e.body.body.tx=1))
          /\ (~run.fault \/ NewAuthority \/ tr.tag#"tx.driver-recover")
     ELSE /\ (3 \in run.tx.published \/
            ~(\E e \in Elements(tr.emissions):e.kind="journal.submit" /\ e.body.body.tx \in {1,2}))
          /\ (tr.tag#"tx.submit.map-close" \/ (T!Holds(run.tx,1,1) /\
            (FaultCut#"old-plan" \/ 2 \in run.tx.begun)))
          /\ ~(\E e \in Elements(tr.emissions):
              e.kind="journal.submit" /\
               ((e.body.kind="begin" /\ e.body.body.tx=2 /\ run.tx.mapOpen /\ FaultCut#"old-plan") \/
                (e.body.kind="reserve" /\ e.body.owner=2 /\ ~NewAuthority)))

TxSteps == {Transition(tr.tag,[run EXCEPT !.tx=tr.next,
              !.network=@ \cup Elements(tr.emissions)],<<>>):
               tr \in {x \in T!Actions(P,run.tx):TxAllowed(x)}}
JournalAllowed(tr) ==
  /\ ~(Scenario="C1" /\ FaultCut#"decision" /\ ~run.fault /\ run.tx.fixed[1][2]=0 /\
       \E r \in tr.next.acceptedHistory:HasFix(r.seq))
  /\ ~(\E c \in {0,1}:tr.next.leaders[c][2].phase="prepare" /\
          run.journal.leaders[c][2].phase="idle" /\
          (c=1 \/ ~run.fault))
  /\ ~(Scenario="C1" /\ ~run.fault /\ tr.tag="journal.learn" /\
         \E v \in Members(0):HasFix(tr.next.learned[0][v].seq))
JournalSteps == {Transition(tr.tag,[run EXCEPT !.journal=tr.next,
                    !.network=@ \cup Elements(tr.emissions)],<<>>):
                  tr \in {x \in J!Actions(JP,run.journal):JournalAllowed(x)}}
OtherSteps == {Transition(tr.tag,[run EXCEPT !.other=tr.next,
                 !.network=@ \cup Elements(tr.emissions)],<<>>):tr \in D!Actions(DP,run.other)}

InputSteps(e) ==
  IF e.kind \in {"journal.submit","journal.recover"}
  THEN IF e.dst=1
       THEN {Transition("input." \o e.kind,[run EXCEPT !.journal=tr.next,
               !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
       ELSE {Transition("input." \o e.kind,[run EXCEPT !.other=tr.next,
               !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in D!Receive(DP,run.other,e)}
  ELSE IF e.kind \in {"journal.deliver","journal.snapshot","tx.fact"}
       THEN {Transition("input." \o e.kind,[run EXCEPT !.tx=tr.next,
               !.restored=@ \/ (e.kind="journal.snapshot" /\ e.body.owner=1),
               !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in T!Receive(P,run.tx,e)}
  ELSE IF e.kind="tx.published"
       THEN {Transition("input.published",[run EXCEPT !.network=@ \ {e}],<<>>)}
  ELSE {}
Inputs == UNION {InputSteps(e):e \in run.network}

Fault ==
  /\ ~run.fault /\ CutReady
  /\ IF Scenario="C1"
     THEN \E crash \in T!CrashActions(P,run.tx):
            \E reset \in (IF FaultCut="media-loss" THEN J!Destroy(JP,run.journal,"A") ELSE J!Reset(JP,run.journal,"A")):
              run'=[run EXCEPT !.tx=T!CrashFold(P,crash.next,1), !.journal=reset.next,
                !.fault=TRUE, !.cutVoters=Cardinality({r.voter:r \in {x \in run.journal.acceptedHistory:x.cfg=0 /\ HasFix(x.seq)}}), !.savedPosition=run.tx.position[1], !.savedBound=run.tx.bounds[1],
                !.crossed=IF FaultCut="decision" THEN run.tx.decision[1]=TxNone
                         ELSE run.tx.fixed[1][2]>0 /\ run.tx.fixed[1][1]=0]
     ELSE run'=[run EXCEPT !.fault=TRUE, !.tx=T!CrashFold(P,run.tx,1),
                !.savedMapPlans=run.tx.oldMapPlans,!.savedBound=run.tx.bounds[1],
                !.crossed=T!Holds(run.tx,1,1) /\ ~T!Holds(run.tx,1,2)]
Handoff ==
  /\ run.fault /\ ~run.handoff
  /\ LET e==Event("handoff","operator",1,"journal.handoff.request",[cfg|->0,target|->1])
     IN \E tr \in J!Receive(JP,run.journal,e):
          run'=[run EXCEPT !.journal=tr.next,!.handoff=TRUE]

RecoverFold ==
  /\ Scenario="C8" /\ run.fault /\ NewAuthority /\ ~run.recoveryRequested
  /\ LET e==Event("recover-fold",T!Fold(1),1,"journal.recover",[reason|->"replacement"])
     IN \E tr \in J!Receive(JP,run.journal,e):
          run'=[run EXCEPT !.journal=tr.next,!.recoveryRequested=TRUE]

Candidates == {tr \in TxSteps \cup JournalSteps \cup OtherSteps \cup Inputs:tr.next#run}
Ordinary ==
  /\ ~(~run.fault /\ CutReady) /\ ~(run.fault /\ ~run.handoff)
  /\ ~(Scenario="C8" /\ run.fault /\ NewAuthority /\ ~run.recoveryRequested)
  /\ Candidates#{}
  /\ LET tr==Choose(Candidates)
     IN run'=tr.next
Done == run.fault /\ NewAuthority /\ run.tx.published=P.transactions /\
        (IF Scenario="C1" THEN run.restored ELSE run.tx.migrated)
Finish == Done /\ UNCHANGED vars
Next == Fault \/ Handoff \/ RecoverFold \/ Ordinary \/ Finish
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)
Completes == <>Done
JournalAgreement == \A x,y \in run.journal.chosenHistory:Comparable(x.seq,y.seq)
JournalEvidence == \A c \in {0,1}:\A v \in Members(c):
  LET q==run.journal.learned[c][v]
  IN Len(q.seq)=0 \/ J!Quorum(JP,run.journal.acceptedHistory,q.cfg,q.ballot,q.seq)
ExactCut == (Scenario="C1" /\ run.fault) =>
 (IF FaultCut="one-accept" THEN run.cutVoters=1 ELSE run.cutVoters>=2)
RestoredBounds == run.restored => run.tx.bounds[1]>=run.savedBound
RestoredPosition == (Scenario="C1" /\ run.restored) => run.tx.position[1]=run.savedPosition
SerialOutcomes == run.tx.foldUp[1] => O!ResultSemantics(P,run.tx)
OutcomeJustified == run.tx.foldUp[1] => O!OutcomeJustified(P,run.tx)
SerialReads == run.tx.foldUp[1] => O!ObservedAtPosition(P,run.tx)
Publication == run.tx.foldUp[1] => O!PublishedSemantics(P,run.tx)
MapTransfer == run.tx.migrated => run.tx.oldMapPlans \subseteq run.tx.published /\ run.tx.bounds[1]>=run.savedBound
RestoredMap == (Scenario="C8" /\ run.restored) =>
  ~run.tx.mapOpen /\ run.tx.oldMapPlans=run.savedMapPlans
EnrollmentFence == (~run.tx.mapOpen) =>
  \A t \in P.transactions \ run.tx.oldMapPlans:run.tx.enrolled[t]={}
NoPositiveTransferredFloor == ~(Scenario="C8" /\ Done /\ run.savedBound>0 /\ run.tx.bounds[1]>=run.savedBound)
NoCompletion == ~Done
NoCut == ~run.crossed
=============================================================================
