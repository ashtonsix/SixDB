----------------------------- MODULE CheckedViews -----------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS Bug, Prepared, TxNone
T == INSTANCE TxKernel
R == INSTANCE RecoveryKernel
V == INSTANCE ViewsKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
S == INSTANCE JournalSchedule
P == [Params("readonly-check",IF Bug="unverified-main" THEN Bug ELSE "none",FALSE)
      EXCEPT !.material=TRUE,!.physicalReads=TRUE,!.externalChecks=TRUE,
             !.externalCompute=IF Bug \in {"main-divergence","unverified-main"} THEN {1,2} ELSE {1}]
RootActor == <<"roots",0>>
HeadRoot == <<0,0>>
ReadRoot == <<2,1>>
Roots == {HeadRoot,ReadRoot}
Data == [t \in {"base","code"} |->
  IF t="base" THEN [kind |-> "base",interpretation |-> "v1",content |-> <<0,0>>]
  ELSE [kind |-> "code",interpretation |-> "v1",content |-> "replace-byte-v1"]]
Recipe == [id |-> "initial",base |-> "base",code |-> "code",patches |-> <<>>,interpretation |-> "v1"]
RootRequest(root,cut,context) ==
 [root |-> root,holders |-> IF root=HeadRoot THEN {11} ELSE {11,12},
  recipe |-> Recipe,cut |-> cut,context |-> context,generation |-> 1,
  requester |-> "binding",view |-> "main",rights |-> [read |-> {1,2},write |-> {}],
  successor |-> ""]
RP == [roots |-> Roots,holders |-> {11,12},data |-> Data,
       initial |-> [h \in {11,12} |-> IF h=11 THEN DOMAIN Data ELSE {}],
       reset |-> FALSE,gc |-> TRUE,abort |-> FALSE,bad |-> "none",retries |-> 1,
       owner |-> 3,actor |-> RootActor]
Views == {"main","check1","check2"}
VP == [actor |-> "views",owner |-> "binding",base |-> <<0,0>>,
 views |-> Views,writers |-> {"writer"},operations |-> {"send"},
 pages |-> {1},pageItems |-> [pg \in {1} |-> {1,2}],
 readScope |-> [v \in Views |-> {1}],opView |-> [o \in {"send"} |-> "check1"],
 opScope |-> [o \in {"send"} |-> {1}],writeScope |-> [w \in {"writer"} |-> {1,2}],
 writeValues |-> [w \in {"writer"} |-> <<1,1>>],noCOW |-> TRUE,reuseWriter |-> "writer",
 restoreRecipe |-> "initial",restoreInterpretation |-> "v1",cancel |-> TRUE,
 crash |-> FALSE,hostReset |-> FALSE,rebind |-> FALSE,prepared |-> Prepared,
 mutant |-> IF Bug \in {"late-user","cancel-retires"} THEN Bug ELSE "none"]
JP == [owners |-> {1,2,3},actors |-> {T!Fold(1),T!Fold(2),RootActor},
       subscribers |-> [a \in {1,2,3} |-> IF a=3 THEN {RootActor} ELSE {T!Fold(a)}],
       initialConfig |-> [a \in {1,2,3} |-> 1]]
VARIABLES tx,roots,views,journal,network,stage,binding,seen
vars == <<tx,roots,views,journal,network,stage,binding,seen>>
Init ==
 /\ tx=T!Init(P) /\ roots=R!Init(RP) /\ views=V!Init(VP) /\ journal=J!Init(JP)
 /\ network={Event("head","binding",RootActor,"root.acquire",RootRequest(HeadRoot,0,"head"))}
 /\ stage=0
 /\ binding=[headGrant |-> FALSE,headBytes |-> FALSE,request |-> TxNone,
              grant |-> TxNone,writer |-> TxNone,installed |-> FALSE,closed |-> {},
              rootClosed |-> {},decisionSent |-> FALSE,reports |-> {},observed |-> {}]
 /\ seen={}

\* These are authored causal cuts, not a reduction theorem for every product
\* interleaving. The old read registers before the write; projection happens
\* after no-COW overwrite, while a physical borrower can outlive root release.
OnlyDelayedHold == \A e \in network:e.kind="root.hold" /\ e.dst=12
Advance ==
 \/ /\ stage=0 /\ binding.headGrant /\ binding.headBytes /\ network={}
    /\ stage'=1 /\ UNCHANGED <<tx,roots,views,journal,network,binding,seen>>
 \/ /\ stage=1 /\ binding.request#TxNone /\ OnlyDelayedHold
    /\ stage'=2 /\ UNCHANGED <<tx,roots,views,journal,network,binding,seen>>
 \/ /\ stage=2 /\ 1 \in tx.published /\ binding.installed /\ OnlyDelayedHold
    /\ stage'=3 /\ UNCHANGED <<tx,roots,views,journal,network,binding,seen>>
 \/ /\ stage=3 /\ 2 \in tx.published /\ binding.closed=Views /\ network={}
    /\ stage'=4 /\ UNCHANGED <<tx,roots,views,journal,network,binding,seen>>
TxActions ==
 IF stage=1 THEN T!SubmitActions(P,tx,2)
 ELSE IF stage=2 THEN T!SubmitActions(P,tx,1) \cup T!PublishActions(P,tx,1) \cup
       UNION {{T!Grant(P,tx,t,a):a \in {s \in P.shards:T!CanGrant(P,tx,t,s)}}:t \in P.transactions}
 ELSE IF stage=3 THEN T!SubmitActions(P,tx,2) \cup T!ReadActions(P,tx,2) \cup
                      T!ComputeActions(P,tx,2) \cup T!PublishActions(P,tx,2)
 ELSE {}
TxStep == /\ TxActions#{}
 /\ LET tr==S!Choose(TxActions) IN
 /\ (~(tr.tag="tx.submit.install") \/ binding.installed)
 /\ tx'=tr.next /\ network'=network \cup Elements(tr.emissions)
 /\ UNCHANGED <<roots,views,journal,stage,binding,seen>>
RuntimeStep == \E tr \in V!Actions(VP,views):
 /\ stage>=2 /\ binding.writer#TxNone
 /\ (stage>=3 \/ tr.tag \in {"close-access-gate","begin-writer","mutate","install"})
 /\ (tr.tag#"backend-retire" \/ roots.roots[ReadRoot].phase="released")
 /\ views'=tr.next /\ network'=network \cup Elements(tr.emissions)
 /\ UNCHANGED <<tx,roots,journal,stage,binding,seen>>
RootActions == {tr \in R!Actions(RP,roots):
 /\ stage=0 \/ stage>=3
 /\ (tr.tag#"begin-physical-delete" \/ stage=4)
 /\ (tr.tag#"copy-immutable-material" \/
       (stage=3 /\ \E token \in DOMAIN Data:
          <<11,12,token>> \in tr.next.copied \ roots.copied))}
RootStep == /\ RootActions#{}
 /\ LET tr==S!Choose(RootActions) IN
 /\ roots'=tr.next /\ network'=network \cup Elements(tr.emissions)
 /\ UNCHANGED <<tx,views,journal,stage,binding,seen>>
JournalStep == /\ J!Actions(JP,journal)#{}
 /\ LET tr==S!Choose(J!Actions(JP,journal)) IN
 /\ journal'=tr.next /\ network'=network \cup Elements(tr.emissions)
 /\ UNCHANGED <<tx,roots,views,stage,binding,seen>>
StartWriter ==
 /\ stage=2 /\ binding.writer=TxNone /\ T!ExecutionReady(P,tx,1)
 /\ binding'=[binding EXCEPT !.writer=[context |-> T!Context(P,tx,1),cut |-> tx.position[1]]]
 /\ UNCHANGED <<tx,roots,views,journal,network,stage,seen>>
DecideWriter ==
 /\ stage=2 /\ ~binding.decisionSent /\ T!Knows(tx,1,"decision",0)
 /\ binding'=[binding EXCEPT !.decisionSent=TRUE]
 /\ network'=network \cup {Event("writer-decision",T!Driver(1),"views","Decision",
       [writer |-> "writer",outcome |-> T!Learned(tx,1,"decision",0)])}
 /\ UNCHANGED <<tx,roots,views,journal,stage,seen>>
CloseRoots == \E r \in Roots \ binding.rootClosed:
 /\ stage=4
 /\ binding'=[binding EXCEPT !.rootClosed=@ \cup {r}]
 /\ network'=network \cup {Event(<<"close",r>>,"binding",RootActor,"root.close",[root |-> r])}
 /\ UNCHANGED <<tx,roots,views,journal,stage,seen>>

ReadOutcome(value,context) ==
 [effects |-> [k \in {} |-> 0],result |-> value,status |-> "ok",context |-> context,
  trace |-> <<<<"query",1,value,context>>>>,calls |-> <<<<"request-query",1,context>>>>,
  outbox |-> <<[id |-> <<context,"result",0>>,value |-> value]>>,children |-> {<<1,1>>}]
MainResult ==
 /\ stage=3 /\ 2 \in P.externalCompute /\ tx.private[2]=TxNone /\ T!ExecutionReady(P,tx,2)
 /\ ~(\E e \in network:e.kind="execution.result" /\ e.body.tx=2)
 /\ \E obs \in {o \in binding.observed:o.view="main"}:
    /\ network'=network \cup {Event("main-result","main",T!Driver(2),"execution.result",
         [tx |-> 2,context |-> obs.context,cut |-> obs.c,
          outcome |-> ReadOutcome(obs.bytes[1]+1,obs.context)])}
    /\ UNCHANGED <<tx,roots,views,journal,stage,binding,seen>>
WriterOutcome(bytes,context) ==
 [effects |-> [k \in {1,2} |-> bytes[k]],result |-> bytes[1],status |-> "ok",context |-> context,
  trace |-> <<>>,calls |-> <<>>,outbox |-> <<[id |-> <<context,"result",0>>,value |-> bytes[1]]>>,
  children |-> {}]

\* Adapters hold only actually received evidence. A caller's view of the root
\* service, a private computation and the decision consumer have separate state.
Adapt(e) ==
 CASE e.kind="root.retain-cut" ->
    [next |-> [binding EXCEPT !.request=e.body],
     emissions |-> <<Event("read-acquire","binding",RootActor,"root.acquire",
       RootRequest(e.body.root,e.body.cut,e.body.context))>>]
 [] e.kind="ViewGrant" /\ e.body.root=HeadRoot ->
    [next |-> [binding EXCEPT !.headGrant=TRUE],emissions |-> <<>>]
 [] e.kind="Material" /\ e.body.root=HeadRoot ->
    [next |-> [binding EXCEPT !.headBytes=TRUE],emissions |-> <<>>]
 [] e.kind="ViewGrant" ->
    [next |-> [binding EXCEPT !.grant=e.body],emissions |->
      <<Event("cut",RootActor,T!Fold(1),"root.cut-protected",[root |-> e.body.root]),
        Event("recipe",RootActor,T!Fold(1),"root.recipe-ready",
          [tx |-> 2,key |-> 1,root |-> e.body.root,cut |-> e.body.c])>>]
 [] e.kind="tx.open-view" ->
    [next |-> binding,emissions |->
      [i \in 1..3 |-> Event(<<"private-view",i>>,"binding","views","ViewGrant",
         [binding.grant EXCEPT !.view=CASE i=1 -> "main" [] i=2 -> "check1" [] OTHER -> "check2"])]]
 [] e.kind="Sealed" ->
    [next |-> binding,emissions |-> <<Event("physical-execution","binding",T!Driver(1),"execution.result",
      [tx |-> 1,context |-> binding.writer.context,cut |-> binding.writer.cut,
       outcome |-> WriterOutcome(e.body.bytes,binding.writer.context)])>>]
 [] e.kind="Installed" -> [next |-> [binding EXCEPT !.installed=TRUE],emissions |-> <<>>]
 [] e.kind="Observe" /\ e.body.view="main" ->
    [next |-> [binding EXCEPT !.observed=@ \cup {e.body}],emissions |->
      <<Event("observed","views",T!Fold(1),"view.observed",
        [tx |-> 2,key |-> 1,cut |-> e.body.c,root |-> ReadRoot,context |-> e.body.context,
         values |-> [k \in {1} |-> e.body.bytes[1]]])>>]
 [] e.kind="Observe" ->
    LET r==IF e.body.view="check1" THEN 1 ELSE 2
        value==e.body.bytes[1]+IF Bug="checker-mismatch" /\ r=2 THEN 1 ELSE 0
        report==[context |-> e.body.context,request |-> "a",outcome |-> ReadOutcome(value,e.body.context)]
    IN [next |-> [binding EXCEPT !.reports=@ \cup {report},!.observed=@ \cup {e.body}],
        emissions |-> <<Event(<<"report",r>>,e.body.view,1,"journal.submit",
          Command(1,T!CID(2,"report",r),"report",[tx |-> 2,key |-> r,data |-> report]))>>]
 [] e.kind="ViewClosed" ->
    [next |-> [binding EXCEPT !.closed=@ \cup {e.body.view}],emissions |-> <<>>]
 [] OTHER -> [next |-> binding,emissions |-> <<>>]
AdapterKinds == {"root.retain-cut","root.source","tx.open-view","Sealed","Installed",
                 "Observe","ViewClosed","tx.published","BackendRetired","BackendCompleted","BorrowStarted","BorrowRetired"}
CopiedClosure == \A t \in DOMAIN Data:R!CopyId(12,t,2) \in DOMAIN roots.copies[12]
Deliver == \E e \in network:
 \/ /\ e.kind \in {"journal.submit","journal.recover"}
    /\ \E tr \in J!Receive(JP,journal,e):
       /\ journal'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<tx,roots,views,stage,binding,seen>>
 \/ /\ e.kind \in {"journal.deliver","journal.snapshot"} /\ e.dst=RootActor
    /\ \E tr \in R!Receive(RP,roots,e):
       /\ roots'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<tx,views,journal,stage,binding,seen>>
 \/ /\ e.kind \in {"journal.deliver","journal.snapshot","tx.fact","root.cut-protected",
                     "root.recipe-ready","view.observed","execution.result"}
    /\ ToString(e.dst)#ToString(RootActor)
    /\ \E tr \in T!Receive(P,tx,e):
       /\ tx'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<roots,views,journal,stage,binding,seen>>
 \/ /\ e.kind \in {"root.acquire","root.hold","root.receipt","root.fetch","root.bytes",
                     "root.close","root.terminal","root.refused","root.custody"}
    /\ (IF e.kind="root.hold" THEN e.dst#12 \/ CopiedClosure ELSE TRUE)
    /\ \E tr \in R!Receive(RP,roots,e):
       /\ roots'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<tx,views,journal,stage,binding,seen>>
 \/ /\ (e.kind="ViewGrant" /\ ToString(e.src)=ToString("binding")) \/ e.kind="Decision" \/
         (e.kind="Material" /\ e.body.root=ReadRoot)
    /\ \E tr \in V!Receive(VP,views,e):
       /\ views'=tr.next /\ network'=(network \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<tx,roots,journal,stage,binding,seen>>
 \/ /\ e.kind \in AdapterKinds \/
         (e.kind="ViewGrant" /\ ToString(e.src)=ToString(RootActor)) \/
         (e.kind="Material" /\ e.body.root=HeadRoot)
    /\ LET tr==Adapt(e) IN
       /\ binding'=tr.next /\ seen'=seen \cup {e}
       /\ network'=(network \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<tx,roots,views,journal,stage>>

\* Drain ordinary service between authored cuts. Named local races remain
\* nondeterministic; this does not fabricate a quorum or a prepared root.
Local == TxStep \/ RootStep \/ RuntimeStep \/ StartWriter \/ DecideWriter \/ CloseRoots \/ MainResult \/ Advance
ServiceStep == Deliver \/ (~ENABLED Deliver /\ JournalStep) \/
               (~ENABLED Deliver /\ ~ENABLED JournalStep /\ Local)
Done == /\ stage=4 /\ tx.published=P.transactions /\ V!Terminal(VP,views)
        /\ \A r \in Roots:roots.roots[r].phase="released"
        /\ \A h \in RP.holders: /\ V!RegistryDebt(roots.registry[h])={}
                                 /\ DOMAIN roots.copies[h]={}
        /\ network={}
Next == ServiceStep \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(ServiceStep)
Completes == <>Done
OldBytes == \A obs \in views.observations:obs.bytes[1]=0 /\ obs.c=tx.position[2]
BorrowedBytes == \A o \in {"send"}:views.operations[o].phase \in {"complete","retired"} =>
                   views.operations[o].result[1]=0
PhysicalDebt == views.operations["send"].charged =
                 (views.operations["send"].phase \in {"queued","active","complete","cancelled"})
RootEvidence == R!HeldExists(RP,roots) /\ R!LiveRetained(RP,roots) /\ R!ExactBytes(RP,roots)
SerialReads == O!ObservedAtPosition(P,tx)
Publication == O!PublishedSemantics(P,tx)
SerialOutcomes == O!ResultSemantics(P,tx)
FailureJustified == O!OutcomeJustified(P,tx)
VerifiedOutcome == tx.decision[2]="commit" =>
  /\ tx.reports[2][1]#TxNone /\ tx.reports[2][1]=tx.reports[2][2]
  /\ tx.reports[2][1].outcome=tx.outcome[2]
PhysicalPublication == 1 \in tx.published =>
  binding.installed /\ \A k \in {1,2}:views.installed[k]=tx.outcome[1].effects[k]
NoLateRead == ~(views.writers["writer"].reused /\ Cardinality(views.observations)=3 /\
                tx.position[2]<tx.position[1])
NoReleasedBorrow == ~(roots.roots[ReadRoot].phase="released" /\ views.operations["send"].charged)
NoMismatchAbort == ~(tx.decision[2]="abort" /\ tx.outcome[2].status="mismatch")
=============================================================================
