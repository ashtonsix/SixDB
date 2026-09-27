------------------------- MODULE CompletionUnderPressure -------------------------
EXTENDS Contracts, Integers
CONSTANTS Bug, Crossing, JournalNone
C == INSTANCE CapacityKernel
R == INSTANCE RecoveryKernel
V == INSTANCE ViewsKernel
J == INSTANCE JournalKernel
S == INSTANCE JournalSchedule
Members == {"A","B","C"}
JP == [owners |-> {"owner"},actors |-> {"roots","decider"},
 subscribers |-> [o \in {"owner"} |-> {"roots","decider"}],
 initialConfig |-> [o \in {"owner"} |-> 0],configs |-> {0},members |-> [c \in {0} |-> Members],
 owner |-> [c \in {0} |-> "owner"],leaders |-> [c \in {0} |-> [b \in {1} |-> "A"]],
 ballots |-> {1},successors |-> [c \in {0} |-> {}],mode |-> IF Bug="early-quorum" THEN "ack_before_persist" ELSE "correct",compaction |-> FALSE,
 atomicVoters |-> {},abstractVoters |-> {},opaqueCertificates |-> FALSE]
Data == [t \in {"base","code"} |-> IF t="base"
 THEN [kind |-> "base",interpretation |-> "v1",content |-> <<17,0>>]
 ELSE [kind |-> "code",interpretation |-> "v1",content |-> "replace-byte-v1"]]
Recipe == [id |-> "source",base |-> "base",code |-> "code",patches |-> <<>>,interpretation |-> "v1"]
Request == [root |-> "read",holders |-> {1},recipe |-> Recipe,cut |-> 3,context |-> "captured-3",
 generation |-> 1,requester |-> "binding",view |-> "extension",
 rights |-> [read |-> {1,2},write |-> {}],successor |-> ""]
RP == [roots |-> {"read"},holders |-> {1,2},data |-> Data,
 initial |-> [h \in {1,2} |-> IF h=1 THEN DOMAIN Data ELSE {}],reset |-> FALSE,gc |-> TRUE,
 abort |-> FALSE,bad |-> "none",retries |-> 1,owner |-> "owner",actor |-> "roots"]
Views == {"extension","repair"}
VP == [actor |-> "views",owner |-> "binding",base |-> <<0,0>>,views |-> Views,writers |-> {},
 operations |-> {"send"},pages |-> {1},pageItems |-> [q \in {1} |-> {1,2}],
 readScope |-> [v \in Views |-> {1}],opView |-> [o \in {"send"} |-> "repair"],
 opScope |-> [o \in {"send"} |-> {1}],writeScope |-> [w \in {} |-> {}],
 writeValues |-> [w \in {} |-> <<>>],noCOW |-> FALSE,reuseWriter |-> "",
 restoreRecipe |-> "unrelated-resident",restoreInterpretation |-> "v1",
 cancel |-> FALSE,crash |-> FALSE,hostReset |-> FALSE,rebind |-> FALSE,prepared |-> FALSE,
 mutant |-> "none"]
Resources == {"ordinary","workers","fault-memory","fault-worker","holder-io","send-io",
              "journal-io","network","metadata","recordA","recordB","recordC"}
Caps == [r \in Resources |-> CASE r \in {"ordinary","workers"} -> 2
 [] r \in {"fault-memory","fault-worker","holder-io","send-io"} -> 1
 [] r="journal-io" -> 6 [] r="network" -> 64 [] r="metadata" -> 10 [] OTHER -> 6]
Zero == [r \in Resources |-> 0]
Admission == [Zero EXCEPT !["ordinary"]=2,!["workers"]=2,
  !["recordA"]=IF Bug="short-record-reservation" THEN 2 ELSE 5,
  !["recordB"]=IF Bug="short-record-reservation" THEN 2 ELSE 5,
  !["recordC"]=IF Bug="short-record-reservation" THEN 2 ELSE 5]
RecordResource(v) == "record" \o v
HistoryDemand == [Zero EXCEPT !["recordA"]=1,!["recordB"]=1,!["recordC"]=1]
InitialPool == [C!PoolInit EXCEPT !.leases=("retained-history" :>
                   C!Lease("retained-history","history","durable-records",HistoryDemand))]
VARIABLES run,unrelated
vars == <<run,unrelated>>
Init ==
 /\ run=[journal |-> J!Init(JP),roots |-> R!Init(RP),views |-> V!Init(VP),
   pool |-> InitialPool,network |-> {},admitted |-> FALSE,filler |-> [v \in Members |-> 1],
   acquired |-> FALSE,fault |-> FALSE,value |-> 0,read |-> FALSE,closed |-> {},
   outcomeSubmitted |-> FALSE,decisionSubmitted |-> FALSE,outcome |-> 0,decision |-> "",
   rootClosed |-> FALSE,subscribers |-> {"required","optional"},cancelled |-> FALSE,
   probe |-> "none",observations |-> {},replied |-> FALSE,finished |-> FALSE]
 /\ unrelated=FALSE
JDebt(st) == Cardinality({v \in Members:st.journal.pending[0][v]#JournalNone})+
             Cardinality(st.journal.callbacks)
HDebt(st) == C!Total([h \in RP.holders |-> [demand |-> [n |-> Cardinality(V!RegistryDebt(st.roots.registry[h]))]]],RP.holders,"n")
\* Journal.net contains retained retry obligations, not wire packets. Its
\* unit here is a bounded message-record slot; this is not byte-buffer sizing.
Demand(st) == [Zero EXCEPT
 !["fault-memory"]=IF st.fault THEN 1 ELSE 0,
 !["fault-worker"]=IF st.fault THEN 1 ELSE HDebt(st),
 !["holder-io"]=HDebt(st),
 !["send-io"]=IF st.views.operations["send"].charged /\ ~(Bug="cancel-debt" /\ st.cancelled) THEN 1 ELSE 0,
 !["journal-io"]=JDebt(st),
 !["metadata"]=JDebt(st)+HDebt(st)+IF st.probe="active" THEN 1 ELSE 0,
 !["network"]=Cardinality(st.journal.net)+Cardinality(st.network)]
\* A lease changes before the corresponding transition can create physical
\* work. The backend lease includes queued writes and undelivered callbacks.
Charged(tr) ==
 IF ~run.admitted THEN {}
 ELSE {Transition(tr.tag,[tr.next EXCEPT !.pool=resize.next],tr.emissions):
        resize \in C!Resize(Caps,run.pool,"backend",Demand(tr.next))}
Use(tr) == /\ run'=tr.next /\ UNCHANGED unrelated
Admit ==
 /\ ~run.admitted
 /\ \E a \in C!Request(Caps,run.pool,C!Lease("admission","obligations","completion",Admission)):
    \E b \in C!Request(Caps,a.next,C!Lease("backend","runtime","physical",Zero)):
      /\ run'=[run EXCEPT !.pool=b.next,!.admitted=TRUE]
      /\ UNCHANGED unrelated
Acquire ==
 IF run.admitted /\ ~run.acquired
 THEN {Transition("application.acquire",[run EXCEPT !.acquired=TRUE,
       !.network=@ \cup {Event("acquire","binding","roots","root.acquire",Request)}],<<>>)} ELSE {}

\* The repair and extension each own an ordinary buffer and parked worker.
\* Dedicated resolver service is the only way to finish their first access.
RootAllowed(tr) ==
 /\ (tr.tag#"copy-immutable-material" \/
      (run.roots.roots["read"].phase="live" /\
       \E token \in DOMAIN Data:<<1,2,token>> \in tr.next.copied \ run.roots.copied))
 /\ (tr.tag#"begin-physical-delete" \/ run.decision="commit")
 /\ (Bug#"no-fault-worker" \/ tr.tag \notin {"backend-start","backend-complete","serve-retained-recipe"})
RootSteps == {Transition(tr.tag,[run EXCEPT !.roots=tr.next,
     !.fault=IF tr.tag="serve-retained-recipe" THEN TRUE ELSE @,
     !.network=@ \cup Elements(tr.emissions)],<<>>):tr \in {x \in R!Actions(RP,run.roots):RootAllowed(x)}}
ViewSteps == {Transition(tr.tag,[run EXCEPT !.views=tr.next,
     !.network=@ \cup Elements(tr.emissions)],<<>>):tr \in {x \in V!Actions(VP,run.views):
       /\ (x.tag#"backend-retire" \/ (run.roots.roots["read"].phase="released" /\
           (Crossing \notin {"cancel","both"} \/ run.cancelled) /\
           (Crossing \notin {"alarm","both"} \/ run.probe#"none")))
       /\ (x.tag#"backend-complete" \/ Crossing#"cancel" \/ run.cancelled)}}
JRecords(st,v) ==
 LET pending==st.journal.pending[0][v]
 IN IF pending=JournalNone THEN Len(J!Full(st.journal,0,v))
    ELSE IF Len(pending.seq)>Len(J!Full(st.journal,0,v)) THEN Len(pending.seq)
    ELSE Len(J!Full(st.journal,0,v))
JournalAllowed(tr) ==
 /\ Bug#"no-control-worker" \/ \A v \in Members:JRecords([run EXCEPT !.journal=tr.next],v)<=2
 /\ \A v \in Members:JRecords([run EXCEPT !.journal=tr.next],v)<=Admission[RecordResource(v)]
JournalSteps == {Transition(tr.tag,[run EXCEPT !.journal=tr.next,
   !.network=@ \cup Elements(tr.emissions)],<<>>):tr \in {x \in J!Actions(JP,run.journal):
     x.next#run.journal /\ JournalAllowed(x)}}
Repaired == \A token \in DOMAIN Data:R!CopyId(2,token,2) \in DOMAIN run.roots.copies[2]
SubmitOutcome ==
 IF run.read /\ Repaired /\ run.views.operations["send"].phase \in {"active","complete"} /\
    ~run.outcomeSubmitted
 THEN {Transition("application.outcome",[run EXCEPT !.outcomeSubmitted=TRUE,
       !.network=@ \cup {Event("outcome","decider","owner","journal.submit",
         Command("owner","outcome","application.outcome",[value |-> run.value+1,context |-> Request.context]))}],<<>>)} ELSE {}
SubmitDecision ==
 IF run.outcome=18 /\ ~run.decisionSubmitted
 THEN {Transition("application.decision",[run EXCEPT !.decisionSubmitted=TRUE,
       !.network=@ \cup {Event("decision","decider","owner","journal.submit",
         Command("owner","decision","application.decision",[outcome |-> "outcome",value |-> "commit"]))}],<<>>)} ELSE {}
CloseRoot ==
 IF run.decision="commit" /\ run.closed=Views /\ ~run.rootClosed
 THEN {Transition("application.close",[run EXCEPT !.rootClosed=TRUE,
       !.network=@ \cup {Event("close","binding","roots","root.close",[root |-> "read"])}],<<>>)} ELSE {}
Reply ==
 IF run.decision="commit" /\ ~run.replied
 THEN {Transition("application.reply",[run EXCEPT !.replied=TRUE],<<>>)} ELSE {}

Input(e) ==
 IF e.kind="journal.submit"
 THEN {Transition("input.journal.submit",[run EXCEPT !.journal=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE IF e.kind="journal.deliver" /\ e.dst="decider"
 THEN {Transition("input.journal.deliver",[run EXCEPT !.network=@ \ {e},
       !.outcome=IF e.body.command.kind="application.outcome" THEN e.body.command.body.value ELSE @,
       !.decision=IF e.body.command.kind="application.decision" THEN e.body.command.body.value ELSE @],<<>>)}
 ELSE IF e.kind="journal.deliver" \/ e.kind \in {"root.acquire","root.hold","root.receipt","root.fetch","root.bytes","root.close","root.terminal","root.refused"}
 THEN {Transition("input." \o e.kind,[run EXCEPT !.roots=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
 ELSE IF e.kind="ViewGrant" /\ e.src="roots"
 THEN {Transition("input.grant",[run EXCEPT !.network=(@ \ {e}) \cup
       {Event(<<"view",v>>,"binding","views","ViewGrant",[e.body EXCEPT !.view=v]):v \in Views}],<<>>)}
 ELSE IF e.kind \in {"ViewGrant","Material"}
 THEN {Transition("input.view",[run EXCEPT !.views=tr.next,
       !.fault=IF e.kind="Material" THEN FALSE ELSE @,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in V!Receive(VP,run.views,e)}
 ELSE IF e.kind="Observe"
 THEN {Transition("input.observe",[run EXCEPT !.network=@ \ {e},!.observations=@ \cup {e.body},
       !.read=IF e.body.view="extension" THEN TRUE ELSE @,
       !.value=IF e.body.view="extension" THEN e.body.bytes[1] ELSE @],<<>>)}
 ELSE IF e.kind="ViewClosed"
 THEN {Transition("input.close",[run EXCEPT !.network=@ \ {e},!.closed=@ \cup {e.body.view}],<<>>)}
 ELSE IF e.kind="BorrowRetired"
 THEN {Transition("input.retire",[run EXCEPT !.network=@ \ {e},!.subscribers={}],<<>>)}
 ELSE {Transition("input.telemetry",[run EXCEPT !.network=@ \ {e}],<<>>)}
Inputs == UNION {Input(e):e \in run.network}

\* Normal service is deliberately scheduled. Cancellation and a false alarm
\* cross that schedule at actual active/undrained borrower cuts.
Candidates == UNION {Charged(tr):tr \in Acquire \cup RootSteps \cup ViewSteps \cup JournalSteps \cup
                     SubmitOutcome \cup SubmitDecision \cup CloseRoot \cup Reply \cup Inputs}
Service == /\ Candidates#{} /\ Use(S!Choose(Candidates))
Cancel ==
 /\ Crossing \in {"cancel","both"} /\ ~run.cancelled
 /\ run.views.operations["send"].phase=IF Crossing="both" THEN "complete" ELSE "active"
 /\ \E tr \in V!Cancel([VP EXCEPT !.cancel=TRUE],run.views,"send"):
    \E next \in Charged(Transition("subscriber.cancel",[run EXCEPT !.views=tr.next,
       !.cancelled=TRUE,!.subscribers=@ \ {"optional"}],<<>>)):Use(next)
Probe ==
 /\ Crossing \in {"alarm","both"} /\ run.probe="none"
 /\ run.views.operations["send"].phase \in {"active","complete"}
 /\ \E next \in Charged(Transition("health.probe",[run EXCEPT !.probe="active"],<<>>)):Use(next)
RetireProbe ==
 /\ run.probe="active" /\ run.decision="commit"
 /\ \E next \in Charged(Transition("health.retire",[run EXCEPT !.probe="retired"],<<>>)):Use(next)
Crossed == /\ Crossing \notin {"cancel","both"} \/ run.cancelled
           /\ Crossing \notin {"alarm","both"} \/ run.probe="retired"
WorkRetired == /\ run.replied /\ V!Terminal(VP,run.views) /\ run.roots.roots["read"].phase="released"
           /\ \A h \in RP.holders:V!RegistryDebt(run.roots.registry[h])={}
           /\ JDebt(run)=0 /\ run.network={} /\ Crossed
Finish ==
 /\ WorkRetired /\ ~run.finished
 /\ \E tr \in C!Resize(Caps,run.pool,"admission",[Admission EXCEPT !["ordinary"]=0,!["workers"]=0]):
    /\ run'=[run EXCEPT !.pool=tr.next,!.finished=TRUE] /\ UNCHANGED unrelated
Retired == run.finished
Unrelated == /\ ~Retired /\ unrelated'=~unrelated /\ UNCHANGED run
Next == Admit \/ Finish \/ Service \/ Cancel \/ Probe \/ RetireProbe \/ Unrelated \/ (Retired /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Admit) /\ WF_vars(Finish) /\ WF_vars(Service) /\
        WF_vars(Cancel) /\ WF_vars(Probe) /\ WF_vars(RetireProbe) /\ WF_vars(Unrelated)
Completes == <>Retired
PoolBounds == \A r \in Resources:C!Usage(run.pool,r)<=Caps[r]
HistoryCharged == \A v \in Members:
 run.pool.leases["retained-history"].demand[RecordResource(v)]=run.filler[v]
ActualRecords == \A v \in Members:JRecords(run,v)+run.filler[v]<=Caps[RecordResource(v)]
PhysicalDebt == run.views.operations["send"].charged =>
   "backend" \in DOMAIN run.pool.leases /\ run.pool.leases["backend"].demand["send-io"]=1
PhysicalWrites == run.admitted =>
   run.pool.leases["backend"].demand["journal-io"]=JDebt(run) /\
   run.pool.leases["backend"].demand["holder-io"]=HDebt(run)
ProtectedMaterial == R!HeldExists(RP,run.roots) /\ R!LiveRetained(RP,run.roots)
ObservedBytes == \A o \in run.observations:o.bytes[1]=17 /\ o.context=Request.context
ActualQuorum(kind) == \E prefix \in {r.seq:r \in run.journal.acceptedHistory}:
 /\ \E command \in Elements(prefix):command.kind=kind
 /\ Cardinality({v \in Members:\E record \in run.journal.acceptedHistory:
       record.voter=v /\ record.cfg=0 /\ record.ballot=1 /\ Prefix(prefix,record.seq)})>=2
OutcomeEvidence == run.outcome#0 => ActualQuorum("application.outcome") /\ run.outcome=18
DecisionEvidence == run.replied => ActualQuorum("application.decision") /\ run.outcome=18
DeliveredEvidence == \A e \in run.journal.outputHistory:e.kind="journal.deliver" =>
 Cardinality({v \in Members:\E r \in run.journal.acceptedHistory:
   r.voter=v /\ r.cfg=0 /\ r.ballot=1 /\ Len(r.seq)>=e.body.index /\
   r.seq[e.body.index]=e.body.command})>=2
FinishedResources == run.finished => C!Usage(run.pool,"ordinary")=0 /\ C!Usage(run.pool,"workers")=0
SharedDebt == run.views.operations["send"].charged => "required" \in run.subscribers
NoCompletion == ~Retired
NoPressure == ~(run.admitted /\ run.fault /\ C!Usage(run.pool,"workers")=2)
NoReleasedBorrow == ~(run.roots.roots["read"].phase="released" /\ run.views.operations["send"].charged)
NoCancelledDebt == ~(run.cancelled /\ run.views.operations["send"].charged /\ "required" \in run.subscribers)
=============================================================================
