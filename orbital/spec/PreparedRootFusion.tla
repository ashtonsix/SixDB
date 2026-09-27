--------------------------- MODULE PreparedRootFusion ---------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS Mode, Fault, CancelAt, Missing, LateBorrow, Bug, TxNone
T == INSTANCE TxKernel
R == INSTANCE RecoveryKernel
V == INSTANCE ViewsKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
S == INSTANCE JournalSchedule
Program == INSTANCE TxProgram

Owner == 1
Actor == T!Fold(Owner)
Root == T!Root(1,1)
RootKinds == {"root.begin","root.register","root.abort","root.release"}
TxKinds == {"begin","snapshot","position","bound","report","outcome","decision"}
Data == [t \in {"base","code","patch"} |-> CASE
 t="base" -> [kind |-> "base",interpretation |-> "v1",content |-> <<9>>]
 [] t="code" -> [kind |-> "code",interpretation |-> "v1",content |-> "replace-byte-v1"]
 [] OTHER -> [kind |-> "patch",interpretation |-> "v1",content |-> [index |-> 1,value |-> 0]]]
Recipe == [id |-> "prepared-genesis",base |-> "base",code |-> "code",
           patches |-> <<"patch">>,interpretation |-> "v1"]
Descriptor == [holders |-> {11},recipe |-> Recipe,generation |-> 1,
              rights |-> [read |-> {1},write |-> {}],terminalOwner |-> Owner]
BaseP == Params("single","none",TRUE)
P == [BaseP EXCEPT !.shards={1},!.keys={1},!.home=[k \in {1}|->1],
 !.writes=[t \in {1}|->{}],!.reads=[t \in {1}|->{1}],!.parts=[t \in {1}|->{}],
 !.dependencies=[k \in {1}|->{1}],!.program=[t \in {1}|->"read"],
 !.causal=[t \in {1}|->5],!.profile=@ @@ [prepared |-> Descriptor],
 !.checked={1},!.material=TRUE,!.physicalReads=TRUE,!.externalChecks=TRUE]
RP == [roots |-> {Root},holders |-> {11},data |-> Data,
 initial |-> [h \in {11}|->DOMAIN Data \ (IF Missing THEN {"code"} ELSE {})],
 reset |-> FALSE,gc |-> TRUE,abort |-> FALSE,bad |-> "none",retries |-> 1,
 owner |-> Owner,actor |-> Actor]
JP == [owners |-> {Owner},actors |-> {Actor},subscribers |-> [o \in {Owner}|->{Actor}],
       initialConfig |-> [o \in {Owner}|->1]]
Views == {"main","check1","check2"}
VP == [actor |-> "views",owner |-> Actor,base |-> <<0>>,views |-> Views,writers |-> {},
 operations |-> {"send"},pages |-> {1},pageItems |-> [pg \in {1}|->{1}],
 readScope |-> [v \in Views|->{1}],opView |-> [o \in {"send"} |-> "main"],
 opScope |-> [o \in {"send"} |-> {1}],writeScope |-> [w \in {}|->{}],
 writeValues |-> [w \in {}|-><<>>],noCOW |-> FALSE,reuseWriter |-> "",
 restoreRecipe |-> "unshared",restoreInterpretation |-> "v1",cancel |-> LateBorrow,
 crash |-> FALSE,hostReset |-> FALSE,rebind |-> FALSE,prepared |-> FALSE,
 mutant |-> IF Bug="early-retire" THEN "cancel-retires" ELSE "none"]
Definition(c) ==
 LET b==c.body d==b.data p==d.context[3].prepared
 IN [root |-> T!Root(b.tx,b.key),holders |-> p.holders,
     recipe |-> IF Bug="wrong-descriptor" THEN [p.recipe EXCEPT !.interpretation="other"] ELSE p.recipe,
     cut |-> d.cut,context |-> d.context,generation |-> p.generation,
     requester |-> Actor,view |-> "main",rights |-> p.rights,
     terminalOwner |-> p.terminalOwner,successor |-> ""]
Projected(c) == Command(c.owner,<<"root.begin",T!Root(c.body.tx,c.body.key)>>,
                         "root.begin",Definition(c))
Acquire(c) == Event(<<"acquire",c.id>>,Actor,Actor,"root.acquire",Definition(c))
DynRP(st) == [RP EXCEPT !.abort=st.cancel]
Forward(es) == {e \in Elements(es):e.kind \notin {"root.retain-cut","root.source","tx.open-view"}}
RECURSIVE RootPrefix(_,_)
RootPrefix(cs,replay) == IF cs= <<>> THEN <<>>
 ELSE (IF Head(cs).kind="bound" /\ Mode="fused" /\ ~(replay /\ Bug="skip-projection")
       THEN <<Projected(Head(cs))>>
       ELSE IF Head(cs).kind \in RootKinds THEN <<Head(cs)>> ELSE <<>>) \o RootPrefix(Tail(cs),replay)
RECURSIVE Discover(_,_)
Discover(st,cs) == IF cs= <<>> THEN st
 ELSE LET n==IF Head(cs).kind="bound" /\ Mode="explicit"
             THEN IF st.roots[Root].phase="vacant"
                  THEN (CHOOSE tr \in R!Receive(RP,[st EXCEPT !.up=TRUE],Acquire(Head(cs))):TRUE).next
                  ELSE st ELSE st
      IN Discover(n,Tail(cs))

VARIABLE run
vars == <<run>>
Init == run=[tx |-> T!Init(P),roots |-> R!Init(RP),views |-> V!Init(VP),
 journal |-> J!Init(JP),network |-> {},cursor |-> 0,prefix |-> <<>>,up |-> TRUE,
 cancel |-> FALSE,cancelSent |-> FALSE,crashed |-> FALSE,recovered |-> FALSE,
 lostReply |-> FALSE,grants |-> {},materials |-> {},observed |-> [v \in {}|->v],
 closed |-> {},rootClose |-> FALSE,published |-> TxNone,projectionEvidence |-> {},
 deliveries |-> {},terminalEvidence |-> {},late |-> FALSE,borrowCancelled |-> FALSE]

Kind(tr) == IF tr.emissions= <<>> THEN "" ELSE
 IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE ""
BoundKnown == \E c \in Elements(run.prefix):c.kind="bound"
Parent(c) == c.kind="bound" /\ c.owner=Owner /\ c.body.tx=1 /\ c.body.key=1
Fold(c) ==
 LET tk==IF c.kind \in TxKinds
          THEN CHOOSE tr \in T!CommitAndFold(P,run.tx,c):TRUE
          ELSE Transition("irrelevant",run.tx,<<>>)
     rr==IF c.kind \in RootKinds THEN R!Apply(DynRP(run),run.roots,c)
         ELSE IF c.kind="bound" THEN
              IF Mode="fused" THEN R!Apply(DynRP(run),run.roots,Projected(c))
              ELSE (CHOOSE tr \in R!Receive(DynRP(run),run.roots,Acquire(c)):TRUE).next
         ELSE run.roots

 IN [run EXCEPT !.tx=tk.next,!.roots=rr,!.prefix=Append(@,c),
     !.cancel=@ \/ c.kind="acquisition.cancel",
     !.network=@ \cup Forward(tk.emissions),
     !.projectionEvidence=IF c.kind="bound" /\ Mode="fused" THEN @ \cup {c} ELSE @,
     !.terminalEvidence=IF c.kind \in {"root.abort","root.release"} THEN @ \cup {c} ELSE @]

(* One owner cursor consumes the real mixed journal. Both semantic folds are
   reconstructed before any service action resumes. The root's physical holders
   and their pending operations are separate processes and survive this cut. *)
Snapshot(e) ==
 LET cs==e.body.prefix
     tk==T!ReplayCommands(P,[run.tx EXCEPT !.foldUp[Owner]=TRUE,!.up[1]=TRUE],
                         SelectSeq(cs,LAMBDA c:c.kind \in TxKinds))
     rootCommands==RootPrefix(cs,TRUE)
     rr==R!Replay(RP,run.roots,rootCommands)
     discovered==IF Bug="skip-discovery" THEN rr ELSE Discover(rr,cs)

 IN [run EXCEPT !.tx=tk.next,!.roots=[discovered EXCEPT !.up=TRUE],
     !.prefix=cs,!.cursor=e.body.index,!.up=TRUE,!.recovered=TRUE,
     !.cancel=(\E c \in Elements(cs):c.kind="acquisition.cancel"),
     !.projectionEvidence=IF Mode="fused" /\ Bug#"skip-projection"
       THEN @ \cup {c \in Elements(cs):c.kind="bound"} ELSE @,
     !.network=(@ \ {e}) \cup Forward(tk.emissions),
     !.deliveries=@ \cup {e},
     !.terminalEvidence=@ \cup {c \in Elements(cs):c.kind \in {"root.abort","root.release"}}]

Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input." \o e.kind,[run EXCEPT !.journal=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE IF e.kind="journal.deliver"
 THEN IF ~run.up THEN {}
      ELSE IF e.body.index<=run.cursor THEN {Transition("input.old", [run EXCEPT !.network=@ \ {e}],<<>>)}
      ELSE IF e.body.index=run.cursor+1
           THEN {Transition("input.journal.deliver",[Fold(e.body.command) EXCEPT
               !.cursor=e.body.index,!.network=@ \ {e},!.deliveries=@ \cup {e}],<<>>)} ELSE {}
 ELSE IF e.kind="journal.snapshot"
 THEN IF ~run.up /\ e.body.owner=Owner /\ e.body.recovery=ToString("fusion-recover")
      THEN {Transition("input.journal.snapshot",Snapshot(e),<<>>)} ELSE {}
 ELSE IF e.kind="RootFailure"
 THEN LET failure==Event(e.id,e.src,e.dst,"root.unavailable",e.body @@ [tx|->1,key|->1])
      IN IF run.up THEN {Transition("input.root-failure",[run EXCEPT !.tx=tr.next,
        !.network=(@ \ {e}) \cup Forward(tr.emissions)],<<>>):tr \in T!Receive(P,run.tx,failure)} ELSE {}
 ELSE IF e.kind="tx.fact" \/ e.kind \in {"root.unavailable","root.cut-protected","view.observed"}
 THEN IF run.up THEN {Transition("input." \o e.kind,[run EXCEPT !.tx=tr.next,
         !.network=(@ \ {e}) \cup Forward(tr.emissions)],<<>>):tr \in T!Receive(P,run.tx,e)} ELSE {}
 ELSE IF e.kind="ViewGrant" /\ ToString(e.dst)=ToString(Actor)
 THEN {Transition("input.root-grant",[run EXCEPT !.grants=@ \cup {e.body},
   !.network=(@ \ {e}) \cup {Event(<<"grant",v>>,Actor,"views","ViewGrant",[e.body EXCEPT !.view=v]):v \in Views} \cup
    {Event("protected",Actor,Actor,"root.cut-protected",[root |-> Root])}],<<>>)}
 ELSE IF e.kind \in {"ViewGrant","Material"}
 THEN {Transition("input.view",[run EXCEPT !.views=tr.next,
       !.materials=IF e.kind="Material" THEN @ \cup {e.body} ELSE @,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in V!Receive(VP,run.views,e)}
 ELSE IF e.kind="Observe"
 THEN {Transition("input.observe",[run EXCEPT !.observed=@ @@ (e.body.view :> e.body),
   !.network=(@ \ {e}) \cup
     (IF e.body.view="main" THEN {Event("view-observed","views",Actor,"view.observed",
       [root |-> Root,tx |-> 1,key |-> 1,cut |-> e.body.c,context |-> e.body.context,
        values |-> [k \in {1}|->e.body.bytes[1]]])} ELSE {})],<<>>)}
 ELSE IF e.kind="ViewClosed"
 THEN {Transition("input.view-close",[run EXCEPT !.closed=@ \cup {e.body.view},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="tx.published"
 THEN {Transition("input.publish",[run EXCEPT !.published=e.body,!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind \in {"BackendCompleted","BackendRetired","BorrowStarted","BorrowRetired"}
 THEN {Transition("input.physical",[run EXCEPT !.network=@ \ {e}],<<>>)}
 ELSE IF (run.up \/ e.dst=11) /\ ~(Fault="hold-reply" /\ ~run.crashed /\ e.kind="root.receipt")
 THEN {Transition("input.root",[run EXCEPT !.roots=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(DynRP(run),run.roots,e)} ELSE {}

CancelRequest ==
 IF CancelAt#"none" /\ ~run.cancelSent /\
    (CASE CancelAt="before" -> run.tx.position[1]>0
      [] CancelAt="after" -> run.roots.roots[Root].phase="live"
      [] OTHER -> run.roots.roots[Root].phase \in {"begun","live"})
 THEN {Transition("application.cancel-acquisition",[run EXCEPT !.cancelSent=TRUE,
   !.network=@ \cup {Event("cancel-acquisition","parent",Owner,"journal.submit",
     Command(Owner,"cancel-acquisition","acquisition.cancel",[root |-> Root,terminalOwner |-> Owner]))}],<<>>)} ELSE {}
Report(r) ==
 LET v==IF r=1 THEN "check1" ELSE "check2"
 IN IF run.up /\ run.tx.private[1]#TxNone /\ run.tx.private[1].status="ok" /\
       v \in DOMAIN run.observed /\ T!CID(1,"report",r) \notin run.tx.sent
 THEN LET outcome==Program!Evaluate(P,1,[k \in {1}|->run.observed[v].bytes[1]],run.observed[v].context)
          tr==T!Request(P,run.tx,1,"report",r,Owner,
                     [context |-> run.observed[v].context,request |-> "same-read",outcome |-> outcome])
      IN {Transition("application.report",[run EXCEPT !.tx=tr.next,
             !.network=@ \cup Elements(tr.emissions)],<<>>)} ELSE {}
CloseRoot ==
 IF run.up /\ run.published#TxNone /\ run.published.decision="commit" /\
    run.closed=Views /\ ~run.rootClose
 THEN {Transition("application.close-root",[run EXCEPT !.rootClose=TRUE,
   !.network=@ \cup {Event("close-root","parent",Actor,"root.close",[root |-> Root])}],<<>>)} ELSE {}

TxSteps == IF run.up THEN
 {Transition(tr.tag,[run EXCEPT !.tx=tr.next,!.network=@ \cup Forward(tr.emissions)],<<>>):
   tr \in {x \in T!Actions(P,run.tx):
       x.tag \notin {"tx.driver-crash","tx.driver-recover"} /\
       (Kind(x)#"bound" \/ CancelAt#"before" \/ run.cancel)}} ELSE {}
RootSteps ==
 {Transition(tr.tag,[run EXCEPT !.roots=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):
  tr \in {x \in R!Actions(DynRP(run),run.roots):
    x.tag#"copy-immutable-material" /\
    (x.tag#"begin-physical-delete" \/ run.roots.roots[Root].phase \in {"live","released","aborted"}) /\
    (Mode#"fused" \/ Kind(x)#"root.begin") /\
    (x.tag#"root-view-grant" \/ CancelAt="none" \/ run.cancel)}}
ViewSteps ==
 {Transition(tr.tag,[run EXCEPT !.views=tr.next,!.network=@ \cup Elements(tr.emissions),
    !.late=@ \/ (tr.tag="backend-complete" /\ run.roots.roots[Root].phase="released"),
    !.borrowCancelled=@ \/ tr.tag="cancel"],<<>>):
   tr \in {x \in V!Actions(VP,run.views):
     (x.tag#"backend-complete" \/ ~LateBorrow \/ (run.roots.roots[Root].phase="released" /\ run.borrowCancelled)) /\
     (x.tag#"backend-retire" \/ ~LateBorrow \/ run.roots.roots[Root].phase="released")}}
JournalSteps ==
 {Transition(tr.tag,[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):
    tr \in J!Actions(JP,run.journal)}
Inputs == UNION {Input(e):e \in run.network}
Candidates == TxSteps \cup RootSteps \cup ViewSteps \cup JournalSteps \cup Inputs \cup
              CloseRoot \cup Report(1) \cup Report(2)
Service == /\ Candidates#{} /\ LET tr==S!Choose(Candidates) IN run'=tr.next

CrashReady == ~run.crashed /\ Fault#"none" /\ BoundKnown /\
 CASE Fault="acquire" -> TRUE
   [] Fault="hold-reply" -> \E e \in run.network:e.kind="root.receipt"
   [] Fault="registered" -> run.roots.roots[Root].phase="live"
   [] Fault="aborted" -> run.roots.roots[Root].phase="aborted"
   [] OTHER -> FALSE
Crash ==
 /\ CrashReady
 /\ LET tk==CHOOSE tr \in T!CrashActions(P,run.tx):TRUE
    IN run'=[run EXCEPT !.tx=T!CrashFold(P,tk.next,Owner),
      !.roots=[R!ColdOwner(RP,run.roots) EXCEPT !.up=FALSE,!.resets=1],
      !.cursor=0,!.prefix= <<>>,!.up=FALSE,!.cancel=FALSE,!.crashed=TRUE,
      !.lostReply=Fault="hold-reply",
      !.network={e \in @:e.kind \notin {"tx.fact","root.receipt","root.unavailable"} /\
                           ~(e.kind="journal.deliver" /\ e.dst=Actor)} \cup
        {Event("fusion-recover",Actor,Owner,"journal.recover",[reason |-> "owner-and-driver-loss"])}]

CancelStep == /\ CancelRequest#{} /\ LET tr==CHOOSE x \in CancelRequest:TRUE IN run'=tr.next
Done == run.published#TxNone /\ run.roots.roots[Root].phase \in {"aborted","released"} /\
        run.roots.holds[11][Root].phase="terminal" /\ V!RegistryDebt(run.roots.registry[11])={} /\
        (run.published.decision="abort" \/ V!Terminal(VP,run.views))
Next == IF CrashReady THEN Crash ELSE
        Service \/ CancelStep \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Service) /\ WF_vars(CancelStep) /\ WF_vars(Crash)
Completes == <>Done

ChosenBound == {c \in Elements(run.journal.log[Owner]):Parent(c)}
DefinitionEvidence ==
 /\ \A c \in run.projectionEvidence:c \in ChosenBound
 /\ run.roots.roots[Root].phase#"vacant" =>
   \E c \in ChosenBound:
     LET b==run.roots.roots[Root].definition p==c.body.data.context[3].prepared
     IN b.cut=c.body.data.cut /\ b.context=c.body.data.context /\ b.holders=p.holders /\
        b.recipe=p.recipe /\ b.rights=p.rights /\ b.terminalOwner=p.terminalOwner
GrantEvidence == \A g \in run.grants:
 \E c \in Elements(run.journal.log[Owner]):
   c.kind="root.register" /\ c.body.root=g.root /\ c.body.cut=g.c /\ c.body.context=g.context
DeliveryEvidence == \A e \in run.deliveries:J!DeliverySound(run.journal.log,e)
PhysicalClosure ==
 /\ R!HeldExists(RP,run.roots)
 /\ run.up => R!LiveRetained(RP,run.roots) /\ R!NoResurrection(RP,run.roots)
 /\ \A r \in run.roots.holderTerminal[11]:run.roots.holds[11][r].phase="terminal"
TxSemantics == O!ObservedAtPosition(P,run.tx) /\ O!ResultSemantics(P,run.tx) /\
               O!OutcomeJustified(P,run.tx) /\ O!PublishedSemantics(P,run.tx)
ExactMaterial == R!ExactBytes(RP,run.roots) /\ \A m \in run.materials:m.bytes= <<0>>
ViewsExact == \A v \in DOMAIN run.observed:run.observed[v].bytes=[k \in {1}|->0]
BorrowDebt == \A o \in V!RegistryDebt(run.views.registry):run.views.operations[o].charged
BorrowBytes == run.views.operations["send"].phase \in {"complete","retired"} =>
 run.views.operations["send"].result= <<0>>
TerminalAuthority == \A c \in run.terminalEvidence:c.body.terminalOwner=Owner /\
   (c.kind="root.release" => run.published#TxNone /\ run.published.decision="commit" /\ run.closed=Views)
Outcome ==
 IF run.published=TxNone THEN TRUE ELSE
 /\ IF run.published.decision="commit"
    THEN run.published.outcome.result=0 /\ DOMAIN run.observed=Views /\ ~Missing /\ CancelAt#"before"
    ELSE run.roots.roots[Root].phase="aborted" /\ (Missing \/ CancelAt \in {"before","race"})
 /\ CancelAt="after" /\ ~Missing => run.published.decision="commit"
BeginCount == Cardinality({i \in 1..Len(run.journal.log[Owner]):run.journal.log[Owner][i].kind="root.begin"})
SavedRound == Done => BeginCount=IF Mode="fused" THEN 0 ELSE 1
RecoveredInventory == run.recovered /\ run.up /\ BoundKnown =>
 Root \in run.roots.requested /\ (Mode="fused" => run.roots.roots[Root].phase#"vacant")
FreshRecoveryMaterial == run.recovered /\ run.published#TxNone /\ run.published.decision="commit" =>
 \E m \in run.materials:"recovery" \in DOMAIN m /\ m.recovery=run.roots.resets
NoRecoveredRead == ~(Done /\ run.recovered /\ run.published.decision="commit")
NoLostReplyRecovery == ~(Done /\ run.recovered /\ run.lostReply)
NoLateSharedBorrow == ~(Done /\ run.late /\ run.borrowCancelled /\ DOMAIN run.observed=Views)
NoSuccessRace == ~(Done /\ CancelAt="race" /\ run.published.decision="commit")
NoAbortRace == ~(Done /\ CancelAt="race" /\ run.published.decision="abort")
Trace == [root |-> run.roots.roots[Root].phase,journal |-> [i \in 1..Len(run.journal.log[Owner]) |-> run.journal.log[Owner][i].kind],
          result |-> run.published,recovered |-> run.recovered,holder |-> run.roots.holds[11][Root],
          observed |-> DOMAIN run.observed,borrow |-> run.views.operations["send"].phase]
=============================================================================
