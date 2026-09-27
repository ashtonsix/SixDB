------------------------- MODULE AuthorityBootstrapSOS -------------------------
EXTENDS Contracts, JournalSchedule
CONSTANTS JournalNone, Bug, Restart
J == INSTANCE JournalKernel
D == INSTANCE DurableLog
R == INSTANCE RecoveryKernel
Data == [t \in {"base","code"}|->IF t="base"
  THEN [kind|->"base",interpretation|->"v1",content|-><<7,9>>]
  ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
Recipe == [id|->"boot",base|->"base",code|->"code",patches|-><<>>,interpretation|->"v1"]
Request == [root|->"boot",holders|->{1},recipe|->Recipe,cut|->1,
  context|->"generation-1",generation|->1,requester|->"D",view|->"scope",
  rights|->[read|->{1,2},write|->{}],successor|->""]
RP == [roots|->{"boot"},holders|->{1},data|->Data,initial|->[h \in {1}|->DOMAIN Data],
  reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->"none",retries|->1,owner|->"roots",actor|->"root-owner"]
DP == [owners|->{"roots"},actors|->{"root-owner"},
 subscribers|->[o \in {"roots"}|->{"root-owner"}],initialConfig|->[o \in {"roots"}|->0]]
Members(c) == IF c=0 THEN {"A","B","C"} ELSE {"D","E","F"}
JP == [owners|->{"control"},actors|->{"D"},subscribers|->[o \in {"control"}|->{"D"}],
 initialConfig|->[o \in {"control"}|->0],configs|->{0,1},
 members|->[c \in {0,1}|->Members(c)],owner|->[c \in {0,1}|->"control"],
 leaders|->[c \in {0,1}|->[b \in {1}|->IF c=0 THEN "A" ELSE "D"]],ballots|->{1},
 successors|->[c \in {0,1}|->IF c=0 THEN {1} ELSE {}],mode|->"correct",
 compaction|->FALSE,opaqueCertificates|->TRUE,abstractVoters|->{"A","B","C","D","E","F"},atomicVoters|->{}]
VARIABLE run,unrelated
vars == <<run,unrelated>>
Init ==
 /\ run=[journal|->J!Init(JP),roots|->R!Init(RP),ledger|->D!Init(DP),network|->{},
   phase|->0,acquired|->FALSE,grants|->{},material|->{},records|->{},snapshot|-><<>>,
   paused|->FALSE,pausePending|->FALSE,pauseCut|->0,blocked|->FALSE,exports|->{},
   reset|->FALSE,bootstrapWithoutAuthority|->FALSE,admissions|->{},admissionEvidence|->{},reopened|->FALSE]
 /\ unrelated=FALSE
MaterialReady ==
 /\ \E g \in run.grants:g.generation=1 /\ g.context="generation-1" /\ g.recipe=Recipe.id
 /\ \E m \in run.material:m.recipe=Recipe.id /\ m.bytes= <<7,9>>
LocalAuthority ==
 /\ Len(run.journal.learned[1]["D"].seq)>0
 /\ \E c \in Elements(run.journal.learned[1]["D"].seq):
       c.kind="control.activate" /\ c.body.generation=1
CanAdmit == LocalAuthority /\ MaterialReady /\ ~run.paused /\ ~run.pausePending
Control(kind,id,body) == Event(id,"D","control","journal.submit",Command("control",id,kind,body))
Inject(e,phase) ==
 \E tr \in J!Receive(JP,run.journal,e):
   /\ run'=[run EXCEPT !.journal=tr.next,!.phase=phase] /\ UNCHANGED unrelated
Acquire ==
 /\ ~run.acquired
 /\ run'=[run EXCEPT !.acquired=TRUE,!.network=@ \cup
    {Event("bootstrap","D","root-owner","root.acquire",Request)}]
 /\ UNCHANGED unrelated
Activate ==
 /\ run.phase=0 /\ MaterialReady
 /\ \E tr \in J!Receive(JP,run.journal,Control("control.activate","activate",[generation|->1,scope|->"scope"])):
      /\ run'=[run EXCEPT !.journal=tr.next,!.phase=1,
          !.bootstrapWithoutAuthority=~LocalAuthority]
      /\ UNCHANGED unrelated
Handoff ==
 /\ run.phase=1 /\ Len(run.journal.learned[0]["A"].seq)>0
 /\ Inject(Event("handoff","operator","control","journal.handoff.request",[cfg|->0,target|->1]),2)
Admit(n) ==
 /\ (n=1 /\ run.phase=2) \/ (n=2 /\ run.phase=9)
 /\ IF Bug="export-authority" /\ n=2 THEN run.exports#{} ELSE CanAdmit
 /\ \E tr \in J!Receive(JP,run.journal,Control("application.write",<<"write",n>>,[value|->n])):
      /\ run'=[run EXCEPT !.journal=tr.next,!.phase=IF n=1 THEN 3 ELSE 10,
          !.admissions=@ \cup {n},!.admissionEvidence=@ \cup
           {[id|->n,authority|->LocalAuthority,material|->MaterialReady,
             paused|->run.paused \/ run.pausePending,reconciled|->run.reopened]}]
      /\ UNCHANGED unrelated
StartSOS ==
 /\ run.phase=3
 /\ \E c \in run.records:c.kind="application.write" /\ c.body.value=1
 /\ run'=[run EXCEPT !.pausePending=TRUE,!.blocked=TRUE,!.phase=4,
                      !.pauseCut=Len(run.journal.learned[1]["D"].seq)]
 /\ UNCHANGED unrelated
PersistPause ==
 /\ run.phase=4 /\ (Bug#"quorum-pause" \/ ~run.blocked)
 /\ run'=[run EXCEPT !.paused=TRUE,!.pausePending=FALSE,!.phase=5]
 /\ UNCHANGED unrelated
\* Admission-controller restart, not a material-holder destruction. Its local
\* pause record and the independent retained physical material survive.
LocalRestart ==
 /\ Restart /\ run.phase=5 /\ ~run.reset
 /\ run'=[run EXCEPT !.records={},!.snapshot= <<>>,!.reset=TRUE,
              !.paused=IF Bug="lose-pause" THEN FALSE ELSE @]
 /\ UNCHANGED unrelated
StrayAdmission ==
 /\ run.phase \in 4..8 /\ CanAdmit /\ 99 \notin run.admissions
 /\ \E tr \in J!Receive(JP,run.journal,Control("application.write","late",[value|->99])):
      /\ run'=[run EXCEPT !.journal=tr.next,!.admissions=@ \cup {99},
            !.admissionEvidence=@ \cup {[id|->99,authority|->LocalAuthority,
                 material|->MaterialReady,paused|->run.paused,reconciled|->run.reopened]}]
      /\ UNCHANGED unrelated
ExportSOS ==
 /\ run.phase=5 /\ run.paused /\ run.blocked /\ (~Restart \/ run.reset)
 /\ run'=[run EXCEPT !.exports=@ \cup
       {[prefix|->J!Full(run.journal,1,"D"),learned|->run.journal.learned[1]["D"].seq,
         material|->run.material,source|->"D",generation|->1,paused|->TRUE]},!.phase=6]
 /\ UNCHANGED unrelated
Heal == /\ run.phase=6 /\ run.exports#{}
        /\ run'=[run EXCEPT !.blocked=FALSE,!.phase=7] /\ UNCHANGED unrelated
ResumeRequest ==
 /\ run.phase=7
 /\ Inject(Control("control.resume","resume",[generation|->1,pauseCut|->run.pauseCut]),8)
RecoverBarrier ==
 /\ run.phase=8 /\ \E c \in run.records:c.kind="control.resume"
 /\ Inject(Event("resume-barrier","D","control","journal.recover",[reason|->"SOS reconciliation"]),9)
Reconcile ==
 /\ run.phase=9 /\ run.paused /\ MaterialReady
 /\ Len(run.snapshot)>run.pauseCut
 /\ \E i \in (run.pauseCut+1)..Len(run.snapshot):
       run.snapshot[i].kind="control.resume" /\ run.snapshot[i].body.generation=1 /\
       run.snapshot[i].body.pauseCut=run.pauseCut
 /\ run'=[run EXCEPT !.paused=FALSE,!.reopened=TRUE] /\ UNCHANGED unrelated

JSteps == IF run.blocked THEN {} ELSE
 {Transition(tr.tag,[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in J!Actions(JP,run.journal)}
RSteps == {Transition(tr.tag,[run EXCEPT !.roots=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in R!Actions(RP,run.roots)}
DSteps == {Transition(tr.tag,[run EXCEPT !.ledger=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in D!Actions(DP,run.ledger)}
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",[run EXCEPT !.ledger=tr.next,
         !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in D!Receive(DP,run.ledger,e)}
 ELSE IF e.kind="journal.deliver" /\ e.dst="D"
 THEN {Transition("input.journal.deliver",[run EXCEPT !.records=@ \cup {e.body.command},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="journal.snapshot" /\ e.dst="D"
 THEN IF e.body.config=1
      THEN {Transition("input.journal.snapshot",[run EXCEPT !.snapshot=e.body.prefix,!.network=@ \ {e}],<<>>)} ELSE {}
 ELSE IF e.kind="ViewGrant"
 THEN {Transition("input.grant",[run EXCEPT !.grants=@ \cup {e.body},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="Material"
 THEN {Transition("input.material",[run EXCEPT !.material=@ \cup {e.body},!.network=@ \ {e}],<<>>)}
 ELSE {Transition("input." \o e.kind,[run EXCEPT !.roots=tr.next,
          !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
Candidates == {tr \in JSteps \cup RSteps \cup DSteps \cup UNION {Input(e):e \in run.network}:tr.next#run}
Service == /\ Candidates#{} /\ run'=Choose(Candidates).next /\ UNCHANGED unrelated
Done == run.phase=10 /\ \E c \in run.records:c.kind="application.write" /\ c.body.value=2
OtherService == /\ ~Done /\ unrelated'=~unrelated /\ UNCHANGED run
Next == Acquire \/ Activate \/ Handoff \/ (\E n \in {1,2}:Admit(n)) \/ StartSOS \/ PersistPause \/
 LocalRestart \/ StrayAdmission \/ ExportSOS \/ Heal \/ ResumeRequest \/ RecoverBarrier \/ Reconcile \/ Service \/ OtherService \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Acquire) /\ WF_vars(Activate) /\ WF_vars(Handoff) /\
 (\A n \in {1,2}:WF_vars(Admit(n))) /\ WF_vars(StartSOS) /\ WF_vars(PersistPause) /\
 WF_vars(LocalRestart) /\ WF_vars(StrayAdmission) /\ WF_vars(ExportSOS) /\ WF_vars(Heal) /\ WF_vars(ResumeRequest) /\ WF_vars(RecoverBarrier) /\
 WF_vars(Reconcile) /\ WF_vars(Service) /\ WF_vars(OtherService)
AdmissionAuthority == \A e \in run.admissionEvidence:
 e.authority /\ e.material /\ ~e.paused /\ (e.id=1 \/ e.reconciled)
HeldMaterial == R!HeldExists(RP,run.roots) /\ R!LiveRetained(RP,run.roots)
MaterialProvenance == \A m \in run.material:m \in run.roots.observations /\ m.bytes= <<7,9>>
SOSProvenance == \A e \in run.exports:
 /\ e.source="D" /\ e.generation=1 /\ e.paused /\ e.material \subseteq run.roots.observations
 /\ Prefix(e.learned,e.prefix)
 /\ \A c \in Elements(e.prefix):c \in run.journal.proposed
NoBootstrap == ~run.bootstrapWithoutAuthority
NoSOS == run.exports={}
Completes == <>Done
SOSWithoutQuorum == <>(run.exports#{})
=============================================================================
