--------------------------- MODULE BootstrapDiscovery --------------------------
EXTENDS Contracts, JournalSchedule
CONSTANTS JournalNone, Bug, Restart
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog
J == INSTANCE JournalKernel
M == INSTANCE MaterialCore
Roots == {"early","late"}
Data == [t \in {"base","code","patch"} |-> CASE
 t="base" -> [kind|->"base",interpretation|->"v1",content|-><<2,4>>]
 [] t="code" -> [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]
 [] OTHER -> [kind|->"patch",interpretation|->"v1",content|->[index|->1,value|->9]]]
Recipe(r) == [id|->r,base|->"base",code|->"code",patches|->IF r="late" THEN <<"patch">> ELSE <<>>,interpretation|->"v1"]
Request(r) == [root|->r,holders|->{IF r="early" THEN 1 ELSE 2},recipe|->Recipe(r),cut|->3,
 context|->"bootstrap",generation|->1,requester|->"reader",view|->r,
 rights|->[read|->{1,2},write|->{}],successor|->"",waitForMaterial|->TRUE]
RP == [roots|->Roots,holders|->{1,2},data|->Data,
 initial|->[h \in {1,2}|->IF h=1 THEN DOMAIN Data ELSE {}],reset|->FALSE,
 gc|->FALSE,abort|->FALSE,bad|->"none",retries|->1,owner|->"roots",actor|->"root-owner"]
DP == [owners|->{"roots"},actors|->{"root-owner","bootstrap"},
 subscribers|->[o \in {"roots"}|->{"root-owner"}],initialConfig|->[o \in {"roots"}|->0]]
JP == [owners|->{"control"},actors|->{"bootstrap"},subscribers|->[o \in {"control"}|->{"bootstrap"}],
 initialConfig|->[o \in {"control"}|->0],configs|->{0},members|->[c \in {0}|->{"A","B","C"}],
 owner|->[c \in {0}|->"control"],leaders|->[c \in {0}|->[b \in {1}|->"A"]],ballots|->{1},
 successors|->[c \in {0}|->{}],mode|->"correct",compaction|->FALSE,
 opaqueCertificates|->TRUE,abstractVoters|->{"A","B","C"},atomicVoters|->{}]
VARIABLE run
vars == <<run>>
Init == run=[roots|->R!Init(RP),ledger|->D!Init(DP),journal|->J!Init(JP),network|->{},
 phase|->0,definitions|->[r \in {}|->r],live|->{},prefix|-><<>>,holds|->{},copies|->{},
 emitted|->{},snapshots|->{},ready|->{},authority|->FALSE,activation|->FALSE,
 sawPartial|->FALSE,lateDuringCopy|->FALSE,reset|->FALSE,exports|->{}]
Acquire(r,phase) ==
 LET e==Event(<<"acquire",r>>,"client","root-owner","root.acquire",Request(r))
 IN /\ run.phase=phase
    /\ \E tr \in R!Receive(RP,run.roots,e):run'=[run EXCEPT !.roots=tr.next,!.phase=phase+1,
         !.lateDuringCopy=IF r="late" THEN run.copies#{} /\ "late" \notin DOMAIN run.definitions ELSE @]
Initial == Acquire("early",0)
Late == /\ run.phase=2 /\ run.copies#{} /\ "early" \in run.live /\ Acquire("late",2)
Query(n,nextPhase) ==
 LET e==Event(<<"discover",n>>,"bootstrap","roots","journal.recover",[query|->n])
 IN \E tr \in D!Receive(DP,run.ledger,e):run'=[run EXCEPT !.ledger=tr.next,!.phase=nextPhase]
InitialQuery == /\ run.phase=1 /\ run.roots.roots["early"].phase="live" /\ Query(1,2)
LateQuery == /\ run.phase=3 /\ run.roots.roots["late"].phase="live" /\ (~Restart \/ run.reset) /\ Query(2,4)
RestartObserver == /\ Restart /\ run.phase=3 /\ ~run.reset
 /\ run'=[run EXCEPT !.definitions=[r \in {}|->r],!.live={},!.prefix= <<>>,!.ready={},!.reset=TRUE]
Definitions(prefix) ==
 LET begins=={c \in Elements(prefix):c.kind="root.begin"}
 IN [r \in {c.body.root:c \in begins}|->(CHOOSE c \in begins:c.body.root=r).body]
Live(prefix) == {r \in DOMAIN Definitions(prefix):
 (\E c \in Elements(prefix):c.kind="root.register" /\ c.body.root=r) /\
 ~(\E c \in Elements(prefix):c.kind \in {"root.release","root.abort"} /\ c.body.root=r)}
Namespace(h,r) == [root|->r,holder|->h,ids|->run.roots.holds[h][r].copies]
ExportNamespace(h,r) ==
 /\ run.phase>=2 /\ 1 \in run.snapshots /\ run.roots.holds[h][r].phase="held"
 /\ <<"namespace",h,r>> \notin run.emitted
 /\ run'=[run EXCEPT !.holds=@ \cup {Namespace(h,r)},
       !.emitted=@ \cup {<<"namespace",h,r>>},
       !.exports=@ \cup {[kind|->"namespace",source|->h,root|->r,ids|->run.roots.holds[h][r].copies]},
       !.authority=IF Bug="export-authority" THEN TRUE ELSE @]
ExportCopy(h,id) ==
 /\ run.phase>=2 /\ id \notin {c.id:c \in run.copies}
 /\ \E b \in run.holds:b.holder=h /\ id \in b.ids
 /\ id \in DOMAIN run.roots.copies[h]
 /\ run'=[run EXCEPT !.copies=@ \cup {run.roots.copies[h][id]},
       !.exports=@ \cup {[kind|->"copy",source|->h,root|->"",ids|->{id}]}]
HolderReady(r,h) ==
 \E b \in run.holds:
 /\ b.root=r /\ b.holder=h
 /\ b.ids \subseteq {c.id:c \in run.copies}
 /\ LET material==[t \in {c.token:c \in {cp \in run.copies:cp.id \in b.ids}}|->
          (CHOOSE c \in run.copies:c.id \in b.ids /\ c.token=t).data]
    IN M!WellTyped(material,run.definitions[r].recipe)
Readable(r) == r \in run.live /\ r \in DOMAIN run.definitions /\
 \A h \in run.definitions[r].holders:HolderReady(r,h)
Classify(r) ==
 /\ r \in DOMAIN run.definitions /\ r \notin run.ready
 /\ Readable(r) \/ (Bug="early-ready" /\ r \in run.live)
 /\ run'=[run EXCEPT !.ready=@ \cup {r}]
Partial ==
 /\ ~run.sawPartial /\ run.live#{} /\ \E r \in run.live:~Readable(r)
 /\ run'=[run EXCEPT !.sawPartial=TRUE]
Activate ==
 /\ run.phase=4 /\ 2 \in run.snapshots /\ ~run.activation
 /\ LET cmd==Command("control","activate","control.activate",[generation|->1,cut|->Len(run.prefix)])
        e==Event("activate","bootstrap","control","journal.submit",cmd)
    IN \E tr \in J!Receive(JP,run.journal,e):run'=[run EXCEPT !.journal=tr.next,!.activation=TRUE]
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",[run EXCEPT !.ledger=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in D!Receive(DP,run.ledger,e)}
 ELSE IF e.kind="journal.snapshot" /\ e.dst="bootstrap"
 THEN LET n==IF e.body.recovery=ToString(<<"discover",1>>) THEN 1 ELSE 2
      IN {Transition("input.journal.snapshot",[run EXCEPT !.prefix=e.body.prefix,!.snapshots=@ \cup {n},
          !.definitions=IF Bug="stale-inventory" /\ n=2 THEN @ ELSE Definitions(e.body.prefix),
          !.live=Live(e.body.prefix),!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="journal.deliver" /\ e.dst="bootstrap" /\
         e.body.owner="control" /\ e.body.command.kind="control.activate"
 THEN {Transition("input.journal.deliver",[run EXCEPT !.authority=TRUE,!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind \in {"ViewGrant","Material"}
 THEN {Transition("consume.root.output",[run EXCEPT !.network=@ \ {e}],<<>>)}
 ELSE {Transition("input." \o e.kind,[run EXCEPT !.roots=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
RootSteps == {Transition(tr.tag,[run EXCEPT !.roots=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):
 tr \in {x \in R!Actions(RP,run.roots):x.tag#"copy-immutable-material" \/ run.phase>=3}}
Steps == {tr \in RootSteps \cup
 {Transition(tr.tag,[run EXCEPT !.ledger=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in D!Actions(DP,run.ledger)} \cup
 {Transition(tr.tag,[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in J!Actions(JP,run.journal)} \cup
 UNION {Input(e):e \in run.network}:tr.next#run}
Service == /\ Steps#{} /\ run'=Choose(Steps).next
\* Actual shared services drain between authored inventory/copy cuts. Export
\* choices remain independent: the consumer starts with no root or token IDs.
Export == (\E h \in {1,2},r \in Roots:ExportNamespace(h,r)) \/
 (\E h \in {1,2}:\E id \in DOMAIN run.roots.copies[h]:ExportCopy(h,id))
Done == run.authority /\ run.ready=Roots /\ run.lateDuringCopy /\ run.sawPartial
Next == IF ENABLED Partial THEN Partial ELSE IF ENABLED Initial THEN Initial
 ELSE IF ENABLED InitialQuery THEN InitialQuery ELSE IF ENABLED Late THEN Late
 ELSE IF ENABLED RestartObserver THEN RestartObserver ELSE IF ENABLED LateQuery THEN LateQuery
 ELSE Service \/ Export \/ (\E r \in DOMAIN run.definitions:Classify(r)) \/ Activate \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)
ReadinessSound == \A r \in run.ready:Readable(r)
InventoryComplete == 2 \in run.snapshots => DOMAIN run.definitions=DOMAIN Definitions(run.prefix)
AuthorityEvidence == run.authority =>
 \E q \in run.journal.chosenHistory:\E c \in Elements(q.seq):c.kind="control.activate"
ExportProvenance == \A cp \in run.copies:
 \E h \in {1,2}:cp.id \in DOMAIN run.roots.copies[h] /\ run.roots.copies[h][cp.id]=cp
RecoveredBytes == \A r \in run.ready:
 \A h \in run.definitions[r].holders:
  \E b \in run.holds:
   /\ b.root=r /\ b.holder=h /\ b.ids \subseteq {c.id:c \in run.copies}
   /\ LET material==[t \in {c.token:c \in {cp \in run.copies:cp.id \in b.ids}}|->
          (CHOOSE c \in run.copies:c.id \in b.ids /\ c.token=t).data]
       IN M!Reconstruct(material,run.definitions[r].recipe)=(IF r="early" THEN <<2,4>> ELSE <<9,4>>)
RootSafety == R!HeldExists(RP,run.roots) /\ R!LiveRetained(RP,run.roots)
Completes == <>Done
NoLateDiscovery == ~(Done /\ "late" \in DOMAIN run.definitions)
NoIndependentScope == ~(run.authority /\ "early" \in run.ready /\
 "late" \in DOMAIN run.definitions /\ "late" \notin run.ready)
NoPartialWithoutAuthority == ~(run.sawPartial /\ ~run.authority)
=============================================================================
