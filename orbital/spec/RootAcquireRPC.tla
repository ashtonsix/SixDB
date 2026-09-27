---------------------------- MODULE RootAcquireRPC ----------------------------
EXTENDS Contracts, JournalSchedule
CONSTANTS Bug, Outcome
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog

\* The owning continuation loses all local intent and response caches after
\* this acquisition has been admitted. It reconstructs the immutable input
\* from the existing chosen root.begin prefix, not a new RPC journal record.
\* A pre-Begin caller loss requires its transaction/client input owner instead.
Data == [t \in {"base","code"}|->IF t="base"
 THEN [kind|->"base",interpretation|->"v1",content|-><<23>>]
 ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
Recipe == [id|->"recipe",base|->"base",code|->"code",patches|-><<>>,interpretation|->"v1"]
Definition == [root|->"root",holders|->{1},recipe|->Recipe,cut|->8,context|->"closure-v1",
 generation|->1,requester|->"reader",view|->"view",rights|->[read|->{1},write|->{}],successor|->""]
P == [roots|->{"root"},holders|->{1},data|->Data,
 initial|->[h \in {1}|->IF Outcome="failure" THEN {"base"} ELSE DOMAIN Data],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->IF Bug="suppress-repeat-acquire" THEN Bug ELSE "none",
 retries|->1,owner|->"owner",actor|->"owner"]
DP == [owners|->{"owner"},actors|->{"owner","reader"},
 subscribers|->[s \in {"owner"}|->{"owner","reader"}],initialConfig|->[s \in {"owner"}|->1]]
Acquire(definition) == Event("immutable-acquire","reader","owner","root.acquire",definition)
ResultKinds == {"ViewGrant","Material","RootFailure"}
VARIABLE y
vars == <<y>>
Init == y=[root|->R!Init(P),log|->D!Init(DP),network|->{},requests|->{Acquire(Definition)},results|->{},
 caller|->[up|->TRUE,known|->TRUE,definition|->Definition,pending|->TRUE,grants|->{},materials|->{},failures|->{}],
 faulted|->FALSE,recovered|->FALSE,recoveryPrefix|-><<>>,deliveries|->{},emitted|->{},postReset|->{},
 retried|->FALSE,ownerBefore|->R!Init(P)]
Route(s,e) == IF e.kind \in ResultKinds THEN [s EXCEPT !.results=@ \cup {e},!.emitted=@ \cup {e}]
 ELSE [s EXCEPT !.network=@ \cup {e}]
RECURSIVE RouteAll(_,_)
RouteAll(s,es) == IF es= <<>> THEN s ELSE RouteAll(Route(s,Head(es)),Tail(es))
Answered(c) == IF Outcome="failure" THEN c.failures#{} ELSE c.grants#{} /\ c.materials#{}
AtCut == ~y.faulted /\ Answered(y.caller)
Crash == /\ AtCut
 /\ y'=[y EXCEPT !.faulted=TRUE,!.ownerBefore=y.root,!.requests={},!.results={},
     !.caller=[up|->FALSE,known|->FALSE,definition|->[missing|->TRUE],pending|->FALSE,
                grants|->{},materials|->{},failures|->{}],
     !.network={e \in @:ToString(e.dst)#ToString("reader")} \cup
       {Event("recover-reader","reader","owner","journal.recover",[recovery|->1])}]
Retry == /\ ~AtCut /\ y.caller.up /\ y.caller.known /\ y.caller.pending
 /\ ~(Bug="one-shot" /\ y.faulted)
 /\ Acquire(y.caller.definition) \notin y.requests
 /\ y'=[y EXCEPT !.requests=@ \cup {Acquire(y.caller.definition)},!.retried=@ \/ y.faulted]
RequestSteps == UNION {{Transition(t.tag,RouteAll([y EXCEPT !.root=t.next,!.requests=@ \ {e}],t.emissions),<<>>):
 t \in R!Receive(P,y.root,e)}:e \in y.requests}
DeliverRequest == /\ ~AtCut /\ RequestSteps#{} /\ y'=Choose(RequestSteps).next
Matches(e) == y.caller.known /\ e.body.root=y.caller.definition.root /\
 CASE e.kind="ViewGrant" -> e.body.c=y.caller.definition.cut /\ e.body.context=y.caller.definition.context /\
      e.body.generation=y.caller.definition.generation /\ e.body.recipe=y.caller.definition.recipe.id
 [] e.kind="Material" -> e.body.recipe=y.caller.definition.recipe.id /\
      e.body.interpretation=y.caller.definition.recipe.interpretation
 [] OTHER -> e.body.cut=y.caller.definition.cut /\ e.body.context=y.caller.definition.context /\
      e.body.generation=y.caller.definition.generation
Accepted(c,e) == [c EXCEPT
 !.grants=IF e.kind="ViewGrant" THEN @ \cup {e} ELSE @,
 !.materials=IF e.kind="Material" THEN @ \cup {e} ELSE @,
 !.failures=IF e.kind="RootFailure" THEN @ \cup {e} ELSE @]
ResultSteps(kind) == {Transition("caller.accept-result",
 LET c==IF Matches(e) THEN Accepted(y.caller,e) ELSE y.caller
 IN [y EXCEPT !.caller=[c EXCEPT !.pending=IF Answered(c) THEN FALSE ELSE @],
      !.results=@ \ {e},!.postReset=IF y.faulted /\ Matches(e) THEN @ \cup {e} ELSE @],<<>>):
 e \in IF y.caller.up THEN {z \in y.results:z.kind=kind} ELSE {}}
DeliverResult(kind) == /\ ~AtCut /\ ResultSteps(kind)#{} /\ y'=Choose(ResultSteps(kind)).next

\* Repeated Acquire can rearm both outputs, so grant and material each receive
\* their own service fairness. Endless retries cannot starve one via the other.
AnswerSteps(kind) == {Transition(t.tag,RouteAll([y EXCEPT !.root=t.next],t.emissions),<<>>):
 t \in {a \in R!Actions(P,y.root):\E e \in Elements(a.emissions):e.kind=kind}}
Answer(kind) == /\ ~AtCut /\ AnswerSteps(kind)#{} /\ y'=Choose(AnswerSteps(kind)).next
RootSteps == {Transition(t.tag,RouteAll([y EXCEPT !.root=t.next],t.emissions),<<>>):
 t \in {a \in R!Actions(P,y.root):a.tag#"copy-immutable-material" /\
   ~(\E e \in Elements(a.emissions):e.kind \in ResultKinds)}}
LogSteps == {Transition(t.tag,RouteAll([y EXCEPT !.log=t.next],t.emissions),<<>>):t \in D!Actions(DP,y.log)}
ReaderInput(e) == IF e.kind="journal.snapshot" /\ ~y.caller.up /\ e.body.recovery=ToString("recover-reader")
 THEN LET begins=={c \in Elements(e.body.prefix):c.kind="root.begin" /\ c.body.requester="reader"}
      IN IF begins#{} THEN
       {Transition("caller.recover-input",[y EXCEPT !.caller.up=TRUE,
          !.caller.known=Bug#"forget-intent",!.caller.definition=(CHOOSE c \in begins:TRUE).body,
          !.caller.pending=TRUE,!.recovered=TRUE,!.recoveryPrefix=e.body.prefix,
          !.network=@ \ {e},!.deliveries=@ \cup {e}],<<>>)} ELSE {}
 ELSE {Transition("caller.consume-prefix",[y EXCEPT !.network=@ \ {e},!.deliveries=@ \cup {e}],<<>>)}
Input(e) == IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",RouteAll([y EXCEPT !.log=t.next,!.network=@ \ {e}],t.emissions),<<>>):
       t \in D!Receive(DP,y.log,e)}
 ELSE IF ToString(e.dst)=ToString("reader") THEN ReaderInput(e)
 ELSE {Transition("input." \o e.kind,RouteAll([y EXCEPT !.root=t.next,!.network=@ \ {e},
       !.deliveries=IF e.kind \in {"journal.deliver","journal.snapshot"} THEN @ \cup {e} ELSE @],t.emissions),<<>>):
       t \in R!Receive(P,y.root,e)}
Candidates == RootSteps \cup LogSteps \cup UNION {Input(e):e \in y.network}
Service == /\ ~AtCut /\ Candidates#{} /\ y'=Choose(Candidates).next
Done == y.faulted /\ y.recovered /\ Answered(y.caller) /\ ~y.caller.pending
Next == Crash \/ Retry \/ DeliverRequest \/ (\E k \in ResultKinds:DeliverResult(k) \/ Answer(k)) \/ Service \/ UNCHANGED vars
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Crash) /\ WF_vars(Retry) /\ WF_vars(DeliverRequest) /\
 (\A k \in ResultKinds:WF_vars(DeliverResult(k)) /\ WF_vars(Answer(k))) /\ WF_vars(Service)
Completes == <>Done
HeldExists == R!HeldExists(P,y.root)
LiveRetained == R!LiveRetained(P,y.root)
DeliverySound == \A e \in y.deliveries:D!DeliverySound(y.log.log,e)
RecoveredInput == y.recovered =>
 /\ y.caller.known
 /\ \E c \in Elements(y.recoveryPrefix):c.kind="root.begin" /\ c.body=y.caller.definition
 /\ y.caller.definition=Definition
OwnerUnaffected == y.faulted =>
 /\ y.root.resets=0 /\ y.root.holderResets={}
 /\ y.ownerBefore.roots["root"].phase=y.root.roots["root"].phase
 /\ (y.ownerBefore.holds[1]["root"].phase="held" => y.root.holds=y.ownerBefore.holds)
NoNewRootDecision == Cardinality({i \in 1..Len(y.log.log["owner"]):
 y.log.log["owner"][i].kind \in {"root.begin","root.register","root.abort","root.release"}})<=2
ResultAuthority == \A e \in y.emitted:
 IF e.kind="RootFailure" THEN
  /\ e.body.root="root" /\ e.body.cut=8 /\ e.body.context="closure-v1" /\ e.body.reason="unavailable"
  /\ \E c \in Elements(y.log.log["owner"]):c.kind="root.abort" /\ c.body.root="root" /\ c.body.reason=e.body.reason
 ELSE
  /\ \E c \in Elements(y.log.log["owner"]):c.kind="root.register" /\ c.body=Definition
  /\ IF e.kind="Material" THEN e.body.bytes= <<23>> /\ e.body.recipe=Recipe.id
     ELSE e.body.c=8 /\ e.body.context="closure-v1" /\ e.body.rights=Definition.rights
SameIdentity == \A e \in y.requests:e=Acquire(Definition)
NoRecoveredResult == ~(Done /\ y.retried /\ y.postReset#{})
=============================================================================
