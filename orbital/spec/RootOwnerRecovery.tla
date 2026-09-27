--------------------------- MODULE RootOwnerRecovery ---------------------------
EXTENDS Contracts, JournalSchedule
CONSTANTS Cut, Bug
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog
Data == [t \in {"base","code"} |-> IF t="base"
 THEN [kind|->"base",interpretation|->"v1",content|-><<17>>]
 ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
Recipe == [id|->"recipe",base|->"base",code|->"code",patches|-><<>>,interpretation|->"v1"]
Definition == [root|->"root",holders|->{1,2},recipe|->Recipe,cut|->7,context|->"profile-v1",
 generation|->1,requester|->"reader",view|->"view",rights|->[read|->{1},write|->{}],successor|->""]
P == [roots|->{"root"},holders|->{1,2},data|->Data,initial|->[h \in {1,2}|->IF Cut \in {"refused","aborted"} THEN {"base"} ELSE DOMAIN Data],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->"none",retries|->1,owner|->"roots",actor|->"owner"]
DP == [owners|->{"roots"},actors|->{"owner"},subscribers|->[o \in {"roots"}|->{"owner"}],
 initialConfig|->[o \in {"roots"}|->1]]
VARIABLE x
vars == <<x>>
Init == x=[root|->R!Init(P),log|->D!Init(DP),network|->{},outputs|->{},acquires|->{},closes|->{},
 crashed|->FALSE,recovered|->FALSE,oldReplies|->{},freshReplies|->{},deliveries|->{},
 freshRefusals|->{},beforeHolderSent|->{},beforeHolds|->[h \in {1,2}|->R!EmptyHold],staleRejected|->FALSE]
Live == x.root.roots["root"].phase="live"
Chosen(kind) == \E c \in Elements(x.log.log["roots"]):c.kind=kind
Query(h) == ToString(<<"fetch","root",h,0,1>>)
AtCut == ~x.crashed /\ CASE
 Cut="hold" -> {b.holder:b \in x.root.receipts}={1,2}
 [] Cut="registered" -> Live /\ Query(1) \in x.root.sent
 [] Cut="fetched" -> x.root.fetched#{}
 [] Cut="close" -> "root" \in x.root.closed
 [] Cut="refused" -> x.root.refusals#{}
 [] Cut="aborted" -> x.root.roots["root"].phase="aborted"
 [] OTHER -> FALSE
Crash ==
 /\ AtCut
 /\ LET n==[R!ColdOwner(P,x.root) EXCEPT !.up=FALSE,!.resets=1]
        e==R!RecoveryEvent(P,1)
    IN x'=[x EXCEPT !.root=n,!.crashed=TRUE,!.beforeHolderSent=x.root.holderSent,
      !.beforeHolds=[h \in {1,2}|->x.root.holds[h]["root"]],
      !.network={m \in @:m.kind#"journal.deliver"} \cup {e} \cup
        {Event(<<"late-reply",b.holder>>,b.holder,"owner","root.receipt",b):b \in x.oldReplies}]
Acquire ==
 /\ x.root.up /\ x.root.resets \notin x.acquires
 /\ x'=[x EXCEPT !.acquires=@ \cup {x.root.resets},!.network=@ \cup
      {Event(<<"acquire",x.root.resets>>,"reader","owner","root.acquire",Definition)}]
Close ==
 /\ x.root.up /\ x.root.resets \notin x.closes
 /\ \E e \in x.outputs:e.kind="Material"
 /\ x'=[x EXCEPT !.closes=@ \cup {x.root.resets},!.network=@ \cup
      {Event(<<"close",x.root.resets>>,"reader","owner","root.close",[root|->"root"])}]
Unavailable(h) == x.crashed /\ Cut="registered" /\ h=1
AdmitStep(t) ==
 /\ ~(Bug="no-requery" /\ x.crashed /\ t.tag \in {"send-root-hold","send-root-fetch"})
 /\ ~(Bug="all-holder-barrier" /\ x.crashed /\ t.tag \in {"root-view-grant","send-root-fetch"} /\
      {b.holder:b \in x.root.receipts}#{1,2})
 /\ ~(Cut="registered" /\ ~x.crashed /\ t.tag="send-root-fetch" /\
       \E e \in Elements(t.emissions):e.dst=2)
 /\ ~(Bug="fixed-holder" /\ t.tag="send-root-fetch" /\
       \E e \in Elements(t.emissions):e.dst=2)
 /\ ~\E e \in Elements(t.emissions):ToString(e.src)=ToString(1) /\ Unavailable(1)
RootSteps == {Transition(t.tag,[x EXCEPT !.root=t.next,!.network=@ \cup Elements(t.emissions)],<<>>):
 t \in {a \in R!Actions(P,x.root):a.tag#"copy-immutable-material" /\ AdmitStep(a)}}
LogSteps == {Transition(t.tag,[x EXCEPT !.log=t.next,!.network=@ \cup Elements(t.emissions)],<<>>):t \in D!Actions(DP,x.log)}
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",[x EXCEPT !.log=t.next,!.network=(@ \ {e}) \cup Elements(t.emissions)],<<>>):t \in D!Receive(DP,x.log,e)}
 ELSE IF e.kind \in {"ViewGrant","Material","RootFailure"}
 THEN {Transition("input.output",[x EXCEPT !.network=@ \ {e},!.outputs=@ \cup {e}],<<>>)}
 ELSE IF e.kind="journal.snapshot" /\ Bug="no-replay"
 THEN {Transition("input.journal.snapshot",[x EXCEPT !.network=@ \ {e},!.root.up=TRUE,!.recovered=TRUE],<<>>)}
 ELSE IF e.kind \in {"root.hold","root.fetch","root.terminal"} /\ Unavailable(e.dst)
 THEN {Transition("input.unreachable-holder",[x EXCEPT !.network=@ \ {e}],<<>>)}
 ELSE {Transition("input." \o e.kind,[x EXCEPT !.root=t.next,!.network=(@ \ {e}) \cup Elements(t.emissions),
   !.deliveries=IF e.kind \in {"journal.deliver","journal.snapshot"} THEN @ \cup {e} ELSE @,
   !.recovered=@ \/ e.kind="journal.snapshot",
   !.oldReplies=IF e.kind="root.receipt" THEN IF e.body.recovery=0 THEN @ \cup {e.body} ELSE @ ELSE @,
   !.freshRefusals=IF e.kind="root.refused" THEN IF e.body.recovery=1 THEN @ \cup {e.body} ELSE @ ELSE @,
   !.freshReplies=IF e.kind="root.receipt" THEN IF e.body.recovery=1 THEN @ \cup {e.body} ELSE @ ELSE @,
   !.staleRejected=IF e.kind="root.receipt" THEN IF x.crashed /\ e.body.recovery=0
       THEN @ \/ t.next.receipts=x.root.receipts ELSE @ ELSE @],<<>>):t \in R!Receive(P,x.root,e)}
Candidates == RootSteps \cup LogSteps \cup UNION {Input(e):e \in x.network}
Service == /\ ~AtCut /\ Candidates#{} /\ x'=Choose(Candidates).next
Done == x.crashed /\ x.recovered /\
 IF Cut \in {"refused","aborted"} THEN x.root.roots["root"].phase="aborted"
 ELSE x.root.roots["root"].phase="released" /\
   \E e \in x.outputs:e.kind="Material" /\ e.body.bytes= <<17>>
\* Waiting remains a legal behavior; liveness detects missing recovery work.
Terminal == UNCHANGED vars
Next == Crash \/ Acquire \/ Close \/ Service \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Crash) /\ WF_vars(Acquire) /\ WF_vars(Close) /\ WF_vars(Service)
Completes == <>Done
HeldExists == R!HeldExists(P,x.root)
LiveRetained == R!LiveRetained(P,x.root)
ExactBytes == R!ExactBytes(P,x.root) /\ \A e \in {o \in x.outputs:o.kind="Material"}:e.body.bytes= <<17>>
NoResurrection == R!NoResurrection(P,x.root)
DeliverySound == \A e \in x.deliveries:D!DeliverySound(x.log.log,e)
HolderPartition == x.crashed => x.beforeHolderSent \subseteq x.root.holderSent
CurrentEvidence == \A b \in x.root.receipts \cup x.root.fetched \cup x.root.refusals:b.recovery=x.root.resets
RecoveredInventory == x.recovered => x.root.roots["root"].phase#"vacant"
FreshHold == (Done /\ Cut="hold") => {b.holder:b \in x.freshReplies}={1,2}
SurvivingRead == (Done /\ Cut="registered") =>
 \E e \in x.outputs:e.kind="Material" /\ e.body.recovery=1 /\ e.body.holder=2
FreshFetch == (Done /\ Cut="fetched") => \E e \in x.outputs:e.kind="Material" /\ e.body.recovery=1
CloseResumed == (Done /\ Cut="close") => 1 \in x.closes
FreshRefusal == (Done /\ Cut="refused") => x.freshRefusals#{}
NoRecoveredRead == ~Done
NoFreshHold == ~({b.holder:b \in x.freshReplies}={1,2} /\ Done)
NoStaleRejection == ~x.staleRejected
=============================================================================
