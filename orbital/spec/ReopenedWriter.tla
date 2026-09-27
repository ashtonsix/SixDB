---------------------------- MODULE ReopenedWriter ----------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS TxNone, Abort, BeforeDecision, Bug
T == INSTANCE TxKernel
R == INSTANCE RecoveryKernel
V == INSTANCE ViewsKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
A(n) == <<"actor",n>>
TP == [Params("single","none",FALSE) EXCEPT !.value[1]=11,!.checked={1},!.externalCompute={1},!.externalChecks=TRUE]
Items == {1,2}
Base == <<0,0>>
Expected == IF Abort THEN Base ELSE <<11,11>>
Data == [t \in {"base","code","recovered"}|->IF t="code" THEN
 [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"] ELSE
 [kind|->"base",interpretation|->"v1",content|->IF t="base" THEN Base ELSE Expected]]
Recipe(t) == [id|->t,base|->t,code|->"code",patches|-><<>>,interpretation|->"v1"]
Roots == {"original","retained","reopened"}
RP == [roots|->Roots,holders|->{11},data|->Data,initial|->[h \in {11}|->{"base","code"}],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->"none",retries|->1,owner|->7,actor|->A("roots")]
Def(r,ctx,c) == [root|->r,holders|->{11},recipe|->Recipe(IF r="reopened" THEN "recovered" ELSE "base"),
 cut|->c,context|->ctx,generation|->1,requester|->A("worker"),view|->IF r="reopened" THEN "view" ELSE r,
 rights|->[read|->Items,write|->{}],successor|->"",waitForMaterial|->TRUE]
WP == [actor|->A("writer"),owner|->A("worker"),base|->Base,views|->{},writers|->{"writer"},operations|->{},
 pages|->{1},pageItems|->[pg \in {1}|->Items],readScope|->[v \in {}|->{}],opView|->[o \in {}|->""],opScope|->[o \in {}|->{}],
 writeScope|->[w \in {"writer"}|->Items],writeValues|->[w \in {"writer"}|-><<11,11>>],noCOW|->TRUE,reuseWriter|->"writer",
 restoreRecipe|->"base",restoreInterpretation|->"v1",cancel|->FALSE,crash|->FALSE,hostReset|->TRUE,rebind|->FALSE,prepared|->FALSE,mutant|->"none"]
VP == [WP EXCEPT !.actor=A("reader"),!.views={"view"},!.writers={},!.readScope=[v \in {"view"}|->Items],
 !.writeScope=[w \in {}|->{}],!.writeValues=[w \in {}|->Base],!.noCOW=FALSE,!.hostReset=FALSE,!.restoreRecipe="unshared"]
JP == [owners|->{1,2,7},actors|->{T!Fold(1),T!Fold(2),A("roots"),A("worker")},
 subscribers|->[o \in {1,2,7}|->IF o=7 THEN {A("roots")} ELSE {T!Fold(o)} \cup (IF o=1 THEN {A("worker")} ELSE {})],
 initialConfig|->[o \in {1,2,7}|->1]]
VARIABLE run
vars == <<run>>
Init == run=[tx|->T!Init(TP),roots|->R!Init(RP),journal|->J!Init(JP),writer|->V!Init(WP),reader|->V!Init(VP),
 network|->{Event("original",A("worker"),A("roots"),"root.acquire",Def("original","head",0))},material|->{},
 cache|->V!RegistryInit,diskCache|->Base,cacheObserved|->Base,cacheRead|->FALSE,reset|->FALSE,sealed|->FALSE,
 queries|->0,snapshot|-><<>>,snapshots|->{},decisionSeen|->FALSE,imported|->FALSE,closed|->FALSE,published|->FALSE,observed|->{},replayed|->{}]
Kind(tr) == IF tr.emissions= <<>> THEN "" ELSE IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE ""
Has(cs,k) == \E c \in Elements(cs):c.kind=k
Record(cs,k) == (CHOOSE c \in Elements(cs):c.kind=k).body.data
Outcome(bytes,ctx) == [effects|->[k \in Items|->bytes[k]],result|->bytes[1],status|->"ok",context|->ctx,trace|-><<>>,calls|-><<>>,
 outbox|-><<[id|-><<ctx,"result",0>>,value|->bytes[1]]>>,children|->{}]
RootCall(r,k,b) == Event(<<k,r>>,A("worker"),A("roots"),k,b)
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"} THEN
 {Transition("input.journal",[run EXCEPT !.journal=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE IF e.kind="journal.deliver" /\ e.dst=A("worker") THEN
 {Transition("input.worker-record",[run EXCEPT !.decisionSeen=@ \/ e.body.command.kind="decision",!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="journal.snapshot" /\ e.dst=A("worker") THEN
 {Transition("input.recovery-prefix",[run EXCEPT !.snapshot=e.body.prefix,!.snapshots=@ \cup {e.body.prefix},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="journal.deliver" /\ e.body.owner=7 THEN
 {Transition("input.root-record",[run EXCEPT !.roots=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
 ELSE IF e.kind \in {"journal.deliver","tx.fact","execution.result"} THEN
 {Transition("input.tx",[run EXCEPT !.tx=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in T!Receive(TP,run.tx,e)}
 ELSE IF e.kind="Sealed" THEN
 LET ctx==T!Context(TP,run.tx,1) outcome==Outcome(e.body.bytes,ctx)
     req==[id|->"cache-write",generation|->run.cache.generation,target|->"instance-cache",kind|->"write",payload|->e.body.bytes]
 IN {Transition("input.actual-seal",[run EXCEPT !.sealed=TRUE,!.cache=tr.next,!.network=(@ \ {e}) \cup
       {Event("physical-outcome",A("writer"),T!Driver(1),"execution.result",[tx|->1,context|->ctx,cut|->run.tx.position[1],outcome|->outcome])} \cup
       {Event(<<"report",r>>,r,1,"journal.submit",Command(1,T!CID(1,"report",r),"report",[tx|->1,key|->r,
         data|->[context|->ctx,request|->IF Abort /\ r=2 THEN "different-call" ELSE "same-call",outcome|->outcome]])):r \in {1,2}}],<<>>):tr \in V!RegistryRegister(run.cache,req)}
 ELSE IF e.kind \in {"ViewGrant","Material"} THEN
 IF e.body.root="reopened" THEN
 {Transition("input.reopened",[run EXCEPT !.reader=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in V!Receive(VP,run.reader,e)}
 ELSE {Transition("input.retained",[run EXCEPT !.material=IF e.kind="Material" THEN @ \cup {e.body} ELSE @,!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="Observe" THEN {Transition("input.read",[run EXCEPT !.observed=@ \cup {e.body},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="ViewClosed" THEN {Transition("input.close",[run EXCEPT !.closed=TRUE,!.network=(@ \ {e}) \cup {RootCall(r,"root.close",[root|->r]):r \in Roots}],<<>>)}
 ELSE IF e.kind="tx.published" THEN {Transition("input.published",[run EXCEPT !.published=TRUE,!.network=@ \ {e}],<<>>)}
 ELSE {Transition("input.root",[run EXCEPT !.roots=tr.next,!.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
Inputs == {e \in run.network:Input(e)#{}}
TxActions == {tr \in T!Actions(TP,run.tx):(~BeforeDecision \/ run.reset \/ Kind(tr)#"outcome") /\ (Kind(tr)#"install" \/ run.observed#{})}
WriterActions == IF run.reset \/ ~T!ExecutionReady(TP,run.tx,1) \/ ~(\E m \in run.material:m.root="original") THEN {}
 ELSE {tr \in V!Actions(WP,run.writer):tr.tag#"host-reset"}
Cache == \E tr \in V!RegistryActions([actor|->A("cache"),owner|->A("worker")],run.cache):
 LET done=={e \in Elements(tr.emissions):e.kind="BackendCompleted"}
 IN run'=[run EXCEPT !.cache=tr.next,
 !.diskCache=IF \E e \in done:e.body.kind="write" THEN (CHOOSE e \in done:TRUE).body.payload ELSE @,
 !.cacheObserved=IF \E e \in done:e.body.kind="read" THEN run.diskCache ELSE @,
 !.cacheRead=@ \/ (\E e \in done:e.body.kind="read")]
Reset ==
 /\ ~run.reset /\ run.sealed /\ run.diskCache= <<11,11>> /\ (BeforeDecision \/ run.decisionSeen)
 /\ \E w \in V!HostReset(WP,run.writer): \E b \in V!RegistryReset(run.cache):
      run'=[run EXCEPT !.writer=w.next,!.cache=b.next,!.reset=TRUE,!.decisionSeen=FALSE,
       !.network=@ \cup {RootCall("retained","root.acquire",Def("retained","head",0))}]
Query ==
 /\ run.reset /\ (run.queries=0 \/ (run.queries=1 /\ run.decisionSeen /\ ~Has(run.snapshot,"decision")))
 /\ run'=[run EXCEPT !.queries=@+1,!.network=@ \cup {Event(<<"recover",run.queries+1>>,A("worker"),1,"journal.recover",[reason|->"lost worker pages"])}]
ReadCache ==
 /\ run.reset /\ "cache-read" \notin DOMAIN run.cache.operations
 /\ \E tr \in V!RegistryRegister(run.cache,[id|->"cache-read",generation|->run.cache.generation,target|->"instance-cache",kind|->"read",payload|-><<>>]):
      run'=[run EXCEPT !.cache=tr.next]
ReopenCache ==
 /\ run.reset
 /\ \E tr \in V!RegistryReopen(run.cache):run'=[run EXCEPT !.cache=tr.next]
Reconstruct ==
 /\ run.reset /\ run.cacheRead /\ ~run.imported /\ Has(run.snapshot,"decision") /\ Has(run.snapshot,"outcome")
 /\ \E base \in run.material:
    /\ base.root="retained"
    /\ LET out==Record(run.snapshot,"outcome") dec==Record(run.snapshot,"decision")
           bytes==IF Bug="trust-cache" THEN run.cacheObserved ELSE IF dec="abort" \/ Bug="skip-replay" THEN base.bytes
                  ELSE [k \in Items|->IF k \in DOMAIN out.effects THEN out.effects[k] ELSE base.bytes[k]]
           cp==R!Copy(11,"recovered",2,[kind|->"base",interpretation|->"v1",content|->bytes])
       IN run'=[run EXCEPT !.imported=TRUE,!.replayed=@ \cup {[base|->base,decision|->dec,outcome|->out,bytes|->bytes,prefix|->run.snapshot]},
        !.network=@ \cup {RootCall("reopened","root.import",[holder|->11,copy|->cp]),
          RootCall("reopened","root.acquire",Def("reopened",out.context,Record(run.snapshot,"position")))}]
Service ==
 IF Inputs#{} THEN LET e==CHOOSE e \in Inputs:TRUE tr==CHOOSE tr \in Input(e):TRUE IN run'=tr.next
 ELSE IF J!Actions(JP,run.journal)#{} THEN LET tr==CHOOSE tr \in J!Actions(JP,run.journal):TRUE
 IN run'=[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF R!Actions(RP,run.roots)#{} THEN LET tr==CHOOSE tr \in R!Actions(RP,run.roots):TRUE
 IN run'=[run EXCEPT !.roots=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF TxActions#{} THEN LET tr==CHOOSE tr \in TxActions:TRUE
 IN run'=[run EXCEPT !.tx=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF WriterActions#{} THEN LET tr==CHOOSE tr \in WriterActions:TRUE
 IN run'=[run EXCEPT !.writer=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE \E tr \in V!Actions(VP,run.reader):run'=[run EXCEPT !.reader=tr.next,!.network=@ \cup Elements(tr.emissions)]
Done == run.reset /\ run.published /\ run.closed /\ (\A r \in Roots:run.roots.roots[r].phase="released") /\ V!RegistryDebt(run.cache)={}
Next == IF ENABLED Reset THEN Reset ELSE IF ENABLED Query THEN Query ELSE
 Service \/ Cache \/ ReadCache \/ ReopenCache \/ Reconstruct \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Completes == <>Done
ReopenedBytes == \A o \in run.observed:o.bytes=Expected /\ o.context=T!Context(TP,run.tx,1)
DurableReplay == \A r \in run.replayed:r.prefix \in run.snapshots /\ Prefix(r.prefix,run.journal.log[1]) /\ r.base \in run.roots.observations
RootClosure == R!HeldExists(RP,run.roots) /\ R!LiveRetained(RP,run.roots)
OutcomeJustified == O!OutcomeJustified(TP,run.tx)
PhysicalOutcomeCorrect == O!ResultSemantics(TP,run.tx)
NoReopened == ~(Done /\ run.observed#{} /\ run.writer.hostReset /\ run.diskCache= <<11,11>>)
NoEarlyReset == ~(run.reset /\ ~Has(run.snapshot,"decision") /\ run.tx.decision[1]=TxNone)
=============================================================================
