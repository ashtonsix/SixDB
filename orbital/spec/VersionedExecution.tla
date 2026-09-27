-------------------------- MODULE VersionedExecution --------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS TxNone, Async, Diverge, Missing, Bug
T == INSTANCE TxKernel
R == INSTANCE RecoveryKernel
V == INSTANCE ViewsKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
Program == INSTANCE TxProgram
A(n) == <<"actor",n>>
P == [Params("single","none",FALSE) EXCEPT !.value[1]=11,!.checked=IF Async THEN {} ELSE {1},
 !.asyncCheck=Async,!.externalCompute={1},!.externalChecks=TRUE]
Versions == {"code-v1","code-v2"}
Token(version,kind) == kind \o ":" \o version
Tokens == {Token(v,k):v \in Versions,k \in {"program","decoder"}}
Data == [t \in Tokens|->IF t \in {Token(v,"decoder"):v \in Versions}
 THEN [kind|->"code",interpretation|->"bytecode-1",content|->"replace-byte-v1"]
 ELSE [kind|->"base",interpretation|->"bytecode-1",content|->IF t=Token("code-v1","program") THEN <<11>> ELSE <<12>>]]
Recipe(version) == [id|->version,base|->Token(version,"program"),code|->Token(version,"decoder"),patches|-><<>>,interpretation|->"bytecode-1"]
Readers == IF Async THEN {"main","audit"} ELSE {"main","check1","check2"}
RP == [roots|->Readers,holders|->{11},data|->Data,
 initial|->[h \in {11}|->IF Missing THEN Tokens \ {Token("code-v1","decoder")} ELSE Tokens],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->"none",retries|->1,owner|->7,actor|->A("roots")]
VP == [actor|->A("views"),owner|->A("binding"),base|-><<0>>,views|->Readers,writers|->{},operations|->{},
 pages|->{1},pageItems|->[pg \in {1}|->{1}],readScope|->[v \in Readers|->{1}],opView|->[o \in {}|->""],opScope|->[o \in {}|->{}],
 writeScope|->[w \in {}|->{}],writeValues|->[w \in {}|-><<0>>],noCOW|->FALSE,reuseWriter|->"",
 restoreRecipe|->"unshared",restoreInterpretation|->"bytecode-1",cancel|->FALSE,crash|->FALSE,hostReset|->FALSE,rebind|->FALSE,prepared|->FALSE,mutant|->"none"]
JP == [owners|->{1,2,7},actors|->{T!Fold(1),T!Fold(2),A("roots")},
 subscribers|->[o \in {1,2,7}|->IF o=7 THEN {A("roots")} ELSE {T!Fold(o)}],initialConfig|->[o \in {1,2,7}|->1]]
VARIABLE run
vars == <<run>>
Init == run=[tx|->T!Init(P),roots|->R!Init(RP),views|->V!Init(VP),journal|->J!Init(JP),network|->{},
 requested|->{},observed|->[v \in {}|->v],executions|->[v \in {}|->v],closed|->FALSE,published|->TxNone,cancelSent|->FALSE,
 unavailable|->TxNone,cachedCode|-><<11>>,closeRequested|->{},auditSubmitted|->FALSE]
CodeContext == <<1,run.tx.profiles[1],P.mapVersion>>
Def(v) == [root|->v,holders|->{11},recipe|->Recipe(IF Bug="wrong-version" THEN "code-v2" ELSE run.tx.profiles[1].sourceVersion),
 cut|->0,context|->CodeContext,generation|->1,requester|->A("binding"),view|->v,rights|->[read|->{1},write|->{}],successor|->""]
\* Binding shares the transaction owner consumer. The root owner is remote:
\* its acquisition failure must arrive as a message, not a phase inspection.
RootCall(v,k,b) == Event(<<k,v>>,A("binding"),A("roots"),k,b)
Kind(tr) == IF tr.emissions= <<>> THEN "" ELSE IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE ""
CodeReady == "main" \in DOMAIN run.observed \/ (Bug="missing-fallback" /\ run.unavailable#TxNone)
TxActions == {tr \in T!Actions(P,run.tx):Kind(tr)#"async-report" /\
 (CodeReady \/ Kind(tr) \in {"begin","cancel-part"} \/ tr.tag="tx.publish")}
Acquire ==
 /\ 1 \in run.tx.begun
 /\ \E v \in Readers \ run.requested:
  /\ v="main" \/ (~Async /\ "main" \in DOMAIN run.observed) \/ (Async /\ run.published#TxNone)
  /\ run'=[run EXCEPT !.requested=@ \cup {v},!.network=@ \cup {RootCall(v,"root.acquire",Def(v))}]
MissingAbort ==
 /\ run.unavailable#TxNone /\ ~run.cancelSent /\ Bug#"missing-fallback"
 /\ LET tr==T!Request(P,run.tx,1,"cancel",0,1,"executable unavailable") IN
    run'=[run EXCEPT !.tx=tr.next,!.cancelSent=TRUE,!.network=@ \cup Elements(tr.emissions)]
Evaluate(v,bytes) ==
 LET value==bytes[1]+IF Diverge /\ v#"main" THEN 1 ELSE 0
     p==[P EXCEPT !.value[1]=value]
 IN Program!Evaluate(p,1,run.tx.inputs[1],T!Context(P,run.tx,1))
Execute ==
 /\ T!ExecutionReady(P,run.tx,1)
 /\ \E v \in Readers \ DOMAIN run.executions:
  /\ v \in DOMAIN run.observed \/ (v="main" /\ Bug="missing-fallback" /\ CodeReady)
  /\ v="main" \/ "main" \in DOMAIN run.executions
  /\ LET actual==v \in DOMAIN run.observed
         bytes==IF actual THEN run.observed[v].bytes ELSE run.cachedCode
         outcome==Evaluate(v,bytes)
         event==IF v="main" THEN Event("main-result",A("binding"),T!Driver(1),"execution.result",
                  [tx|->1,context|->T!Context(P,run.tx,1),cut|->run.tx.position[1],outcome|->outcome])
                ELSE IF Async THEN Event("audit-result",A("binding"),1,"journal.submit",
                  Command(1,T!CID(1,"async-report",0),"async-report",[tx|->1,key|->0,data|->[context|->outcome.context,result|->outcome.result]]))
                ELSE LET r==IF v="check1" THEN 1 ELSE 2 IN
                  Event(<<"report",r>>,A("binding"),1,"journal.submit",Command(1,T!CID(1,"report",r),"report",
                    [tx|->1,key|->r,data|->[context|->outcome.context,request|->"same-call",outcome|->outcome]]))
     IN run'=[run EXCEPT !.executions=@ @@ (v :> [outcome|->outcome,bytes|->bytes,profile|->run.tx.profiles[1],
                       observation|->IF actual THEN run.observed[v] ELSE TxNone]),
                       !.auditSubmitted=@ \/ v="audit",!.network=@ \cup {event}]
ReleaseReady == run.published#TxNone /\ (~Async \/ run.tx.asyncReports[1]#TxNone \/ Bug="early-audit-release")
Close ==
 /\ ReleaseReady
 /\ \E v \in run.requested \ run.closeRequested:
      run'=[run EXCEPT !.closeRequested=@ \cup {v},!.network=@ \cup {RootCall(v,"root.close",[root|->v])}]
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"} THEN
 {Transition("input.journal",[run EXCEPT !.journal=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE IF e.kind="journal.deliver" /\ e.body.owner=7 THEN
 {Transition("input.root",[run EXCEPT !.roots=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions) \cup
   (IF e.body.command.kind="root.abort" THEN
      {Event(<<"code-unavailable",e.body.command.id>>,A("roots"),A("binding"),"code.unavailable",e.body.command.body)} ELSE {})],<<>>):tr \in R!Receive(RP,run.roots,e)}
 ELSE IF e.kind="code.unavailable" THEN
 IF e.body.root="main" /\ e.body.context=CodeContext
 THEN {Transition("input.unavailable",[run EXCEPT !.unavailable=e.body,!.network=@ \ {e}],<<>>)} ELSE {}
 ELSE IF e.kind \in {"journal.deliver","tx.fact","execution.result"} THEN
 {Transition("input.tx",[run EXCEPT !.tx=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in T!Receive(P,run.tx,e)}
 ELSE IF e.kind \in {"ViewGrant","Material"} THEN
 {Transition("input.view",[run EXCEPT !.views=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in V!Receive(VP,run.views,e)}
 ELSE IF e.kind="Observe" THEN {Transition("input.code",[run EXCEPT !.observed=@ @@ (e.body.view :> e.body),!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="ViewClosed" THEN {Transition("input.close-view",[run EXCEPT !.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="tx.published" THEN {Transition("input.publication",[run EXCEPT !.published=e.body,!.network=@ \ {e}],<<>>)}
 ELSE {Transition("input.root",[run EXCEPT !.roots=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
Inputs == {e \in run.network:Input(e)#{}}
Service ==
 IF Inputs#{} THEN LET e==CHOOSE e \in Inputs:TRUE tr==CHOOSE tr \in Input(e):TRUE IN run'=tr.next
 ELSE IF J!Actions(JP,run.journal)#{} THEN LET tr==CHOOSE tr \in J!Actions(JP,run.journal):TRUE IN
 run'=[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF R!Actions(RP,run.roots)#{} THEN LET tr==CHOOSE tr \in R!Actions(RP,run.roots):TRUE IN
 run'=[run EXCEPT !.roots=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF V!Actions(VP,run.views)#{} THEN LET tr==CHOOSE tr \in V!Actions(VP,run.views):TRUE IN
 run'=[run EXCEPT !.views=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF TxActions#{} THEN LET tr==CHOOSE tr \in TxActions:TRUE IN
 run'=[run EXCEPT !.tx=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE FALSE
Done == /\ run.published#TxNone
 /\ (IF Missing /\ Bug#"missing-fallback" THEN run.tx.cancelled[1] /\ run.roots.roots["main"].phase="aborted"
     ELSE (\A v \in Readers:run.roots.roots[v].phase="released") /\ (Async => run.tx.asyncReports[1]#TxNone))
Next == IF ENABLED Close THEN Close ELSE Service \/ Acquire \/ Execute \/ MissingAbort \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Completes == <>Done
CodeVersion == \A v \in DOMAIN run.executions:
 LET x==run.executions[v] IN
 /\ x.observation \in run.views.observations
 /\ x.observation.recipe=x.profile.sourceVersion
 /\ x.observation.context= <<1,x.profile,P.mapVersion>>
 /\ x.bytes=[i \in {1}|->IF x.profile.sourceVersion="code-v1" THEN 11 ELSE 12]
 /\ \E m \in run.roots.observations:m.root=v /\ m.recipe=x.profile.sourceVersion /\ m.bytes=x.bytes
CodeRetention ==
 (Async /\ run.published#TxNone /\ ~run.tx.cancelled[1] /\ run.tx.asyncReports[1]=TxNone) => run.roots.roots["main"].phase="live"
AuditMeaning ==
 /\ (run.tx.asyncReports[1]#TxNone => "audit" \in DOMAIN run.executions /\ run.tx.asyncReports[1].result=run.executions["audit"].outcome.result)
 /\ (Async /\ Diverge /\ run.tx.asyncReports[1]#TxNone => 1 \in run.tx.incidents)
 /\ (run.published#TxNone => run.tx.outcome[1]=run.published.outcome /\ run.tx.decision[1]=run.published.decision)
RootClosure == R!HeldExists(RP,run.roots) /\ R!LiveRetained(RP,run.roots)
OutcomeJustified == O!OutcomeJustified(P,run.tx)
MissingProvenance == run.tx.cancelled[1] =>
 /\ run.unavailable#TxNone
 /\ \E c \in Elements(run.journal.log[7]):c.kind="root.abort" /\ c.body=run.unavailable
SerialOutcomes == O!ResultSemantics(P,run.tx)
JournaledProfile == \A v \in DOMAIN run.executions:
 \E c \in Elements(run.journal.log[1]):c.kind="begin" /\ c.body.data.profile=run.executions[v].profile
NoAuditIncident == ~(Done /\ 1 \in run.tx.incidents)
NoProtectedPublication == ~(Async /\ run.published#TxNone /\ run.tx.asyncReports[1]=TxNone /\ run.roots.roots["main"].phase="live")
NoMissingAbort == ~(Done /\ run.tx.cancelled[1] /\ DOMAIN run.executions={})
=============================================================================
