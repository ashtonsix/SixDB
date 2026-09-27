------------------------------ MODULE PolicyTxBinding ------------------------------
EXTENDS Contracts, TxFixtures, Integers
CONSTANTS TxNone, Policy, Reset, Bug
K == INSTANCE TxKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
S == INSTANCE JournalSchedule
P == [Params("ordinary","none",FALSE) EXCEPT
 !.writes=[t \in 1..3 |-> CASE t=1 -> {1,2} [] t=2 -> {1,3} [] OTHER -> {3}],
 !.parts=[t \in 1..3 |-> IF t=1 THEN {1,2} ELSE {1}],
 !.reads=[t \in 1..3 |-> {}],!.program=[t \in 1..3 |-> "put"],
 !.value=[t \in 1..3 |-> t]] @@ [queuePolicy |-> Policy]
JP == [owners |-> P.shards,actors |-> {K!Fold(a):a \in P.shards},
 subscribers |-> [a \in P.shards |-> {K!Fold(a)}],initialConfig |-> [a \in P.shards |-> 1]]
Wire(e) == [a |-> ToString(<<e.kind,IF e.kind="tx.fact" THEN e.body.kind ELSE "">>)] @@
 (IF e.kind="journal.submit" THEN [e EXCEPT !.body=[a |-> @.kind] @@ @] ELSE e)
WireEvents(es) == {Wire(es[i]):i \in 1..Len(es)}
IsGrant(e,t) == e.kind="tx.fact" /\ e.body.kind="grant" /\ e.body.tx=t /\ e.body.key=1
Reserve3 == ToString(K!CID(3,"reserve",1))
WithoutGrant(es) == SelectSeq(es,LAMBDA e:~IsGrant(e,3))
VARIABLE run
vars == <<run>>
Init == run=[tx |-> K!Init(P),journal |-> J!Init(JP),network |-> {},held |-> TxNone,
 prefix |-> <<>>,outputs |-> <<>>,crashed |-> FALSE,recovered |-> FALSE,
 released |-> FALSE,client |-> {},sawBypass |-> FALSE,replayOutput |-> TRUE]
Remote(e) == e.kind="journal.submit" /\ e.body.kind="reserve" /\ e.body.owner=2 /\ e.body.body.tx=1
IndependentDone == \E r \in run.client:r.tx=3 /\ r.decision="commit" /\ r.outcome.result=3
BeginAllowed(tr) == tr.tag#"tx.submit.begin" \/
 CASE Head(tr.emissions).body.body.tx=2 -> run.held#TxNone
 [] Head(tr.emissions).body.body.tx=3 -> run.tx.ticket[2][1]="queued"
 [] OTHER -> TRUE
TxChoices == IF run.tx.foldUp[1] THEN
 {Transition(tr.tag,[run EXCEPT !.tx=tr.next,!.network=@ \cup WireEvents(tr.emissions)],<<>>):
  tr \in {x \in K!Actions(P,run.tx):BeginAllowed(x)}} ELSE {}
JChoices == {Transition(tr.tag,[run EXCEPT !.journal=tr.next,
 !.network=@ \cup WireEvents(tr.emissions)],<<>>):tr \in J!Actions(JP,run.journal)}
Mutate(tr,c) ==
 IF c.kind="reserve" /\ c.body.tx=3 /\ c.owner=1 /\ Bug="deferred"
 THEN [tr EXCEPT !.next.queue[1]=Append(@,3),!.next.ticket[3][1]="queued",
   !.next.grantOrder[1]=K!Remove(@,3),!.next.replies[c.id]=WithoutGrant(@),
   !.emissions=WithoutGrant(@)]
 ELSE IF c.kind="reserve" /\ c.body.tx=3 /\ c.owner=1 /\ Bug="reply-cache"
 THEN [tr EXCEPT !.next.replies[c.id]=WithoutGrant(@)] ELSE tr
Deliver(e) ==
 IF Remote(e) /\ ~run.released
 THEN {Transition("hold-real-remote-reserve",[run EXCEPT !.held=e,!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input." \o e.kind,[run EXCEPT !.journal=tr.next,
      !.network=(@ \ {e}) \cup WireEvents(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE IF e.kind="journal.deliver"
 THEN {LET step==Mutate(tr,e.body.command)
       IN Transition("input.journal.deliver",[run EXCEPT !.tx=step.next,
          !.network=(@ \ {e}) \cup WireEvents(step.emissions),
          !.prefix=IF e.body.owner=1 THEN Append(@,e.body.command) ELSE @,
          !.outputs=IF e.body.owner=1 THEN @ \o step.emissions ELSE @,
          !.sawBypass=(@ \/ (run.held#TxNone /\ step.next.ticket[2][1]="queued" /\
                              K!Holds(step.next,1,1) /\ K!Holds(step.next,3,1)))],<<>>):
       tr \in K!Receive(P,run.tx,e)}
 ELSE IF e.kind="journal.snapshot"
 THEN {LET es==IF Bug="lost-replay-grant" THEN WithoutGrant(tr.emissions) ELSE tr.emissions
           ref==K!ReplayCommands(P,K!Init(P),e.body.prefix)
       IN Transition("input.journal.snapshot",[run EXCEPT !.tx=tr.next,
           !.network=(@ \ {e}) \cup WireEvents(es),!.prefix=e.body.prefix,
           !.outputs=es,!.recovered=TRUE,!.replayOutput=(es=ref.emissions)],<<>>):
          tr \in K!Receive(P,run.tx,e)}
 ELSE IF e.kind="tx.fact"
 THEN {Transition("input.tx.fact",[run EXCEPT !.tx=tr.next,
       !.network=(@ \ {e}) \cup WireEvents(tr.emissions)],<<>>):tr \in K!Receive(P,run.tx,e)}
 ELSE IF e.kind="tx.published"
 THEN {Transition("input.client",[run EXCEPT !.client=@ \cup {e.body},!.network=@ \ {e}],<<>>)}
 ELSE {}
Inputs == UNION {Deliver(e):e \in run.network}
Candidates == TxChoices \cup JChoices \cup Inputs
Service == /\ Candidates#{} /\ LET tr==S!Choose(Candidates) IN run'=tr.next
CrashReady == Reset /\ ~run.crashed /\ K!Holds(run.tx,3,1) /\ ~K!Knows(run.tx,3,"grant",1)
Crash == /\ CrashReady
 /\ run'=[run EXCEPT !.tx=K!CrashFold(P,run.tx,1),!.crashed=TRUE,!.outputs= <<>>,!.prefix= <<>>,
   !.network={e \in @:~(e.kind="tx.fact" /\ e.src=K!Fold(1))} \cup
      {Wire(Event("policy-recover",K!Fold(1),1,"journal.recover",[reason |-> "local-fold-loss"]))}]
ReleaseRemote == /\ IndependentDone /\ ~run.released /\ run.held#TxNone
 /\ run'=[run EXCEPT !.network=@ \cup {run.held},!.released=TRUE]
Done == run.tx.published=P.transactions /\ run.network={} /\ run.released
Next == IF CrashReady THEN Crash ELSE Service \/ ReleaseRemote \/ (UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Service) /\ WF_vars(ReleaseRemote)
Completes == <>Done
IndependentCompletes == <>IndependentDone
Meaning == O!ResultSemantics(P,run.tx) /\ O!PublishedSemantics(P,run.tx)
Exclusive == \A t,u \in P.transactions:\A a \in P.shards:
 t#u /\ K!Holds(run.tx,t,a) /\ K!Holds(run.tx,u,a) => ~K!Conflict(P,t,u,a)
Closed == run.tx.foldUp[1] => ~\E t \in P.transactions:K!CanGrant(P,run.tx,t,1)
CacheAgreement == run.tx.foldUp[1] =>
 LET ref==K!ReplayCommands(P,K!Init(P),run.prefix)
     ids=={c.id:c \in Elements(run.prefix)}
 IN /\ (\A id \in ids:run.tx.replies[id]=ref.next.replies[id])
    /\ run.outputs=ref.emissions
ReplayOutput == run.replayOutput
RemoteBoundary == ~run.released /\ run.held#TxNone =>
 Remote(run.held) /\ (K!Holds(run.tx,1,1) \/ ~run.tx.foldUp[1])
UsefulBypass == IndependentDone /\ ~run.released =>
 /\ run.sawBypass /\ run.tx.position[1]=0 /\ run.tx.ticket[2][1]="queued"
 /\ \E v \in run.tx.versions:v.tx=3 /\ v.key=3 /\ v.value=3
NoBypass == ~run.sawBypass
NoRecoveredCompletion == ~(run.recovered /\ Done)
(* Chosen records and recovery snapshots are actual DurableLog deliveries. The
   authored service order holds one real remote reservation envelope, then drains
   otherwise ordinary service through shared kernels. Completion of Tx3 releases
   that envelope; the ordered comparator is expected to stall before that cut.
   CacheAgreement is a replay correspondence check using the same semantic fold;
   Meaning and UsefulBypass independently check the resulting client behavior. *)
=============================================================================
