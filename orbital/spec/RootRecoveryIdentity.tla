------------------------- MODULE RootRecoveryIdentity -------------------------
EXTENDS Contracts, JournalSchedule
CONSTANT Bug
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog
Owners == {"alpha","beta"}
Definition(a) == [root|->a,holders|->{1},recipe|->[id|->a,base|->"b",code|->"c",patches|-><<>>,interpretation|->"v1"],
 cut|->1,context|->a,generation|->1,requester|->"client",view|->a,rights|->[read|->{1},write|->{}],successor|->""]
P(a) == [roots|->{a},holders|->{1},data|->[t \in {}|->t],initial|->[h \in {1}|->{}],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->"none",retries|->1,owner|->a,actor|->a]
DP == [owners|->Owners,actors|->Owners,subscribers|->[a \in Owners|->{a}],initialConfig|->[a \in Owners|->1]]
VARIABLE x
vars == <<x>>
Init == x=[roots|->[a \in Owners|->R!Init(P(a))],log|->D!Init(DP),crashed|->FALSE,delivered|->{},
 network|->{Event(<<"acquire",a>>,"client",a,"root.acquire",Definition(a)):a \in Owners}]
Ready == ~x.crashed /\ \A a \in Owners:x.roots[a].roots[a].phase="begun"
RecoverEvent(a) == LET e==R!RecoveryEvent(P(a),1)
 IN IF Bug="unscoped" THEN [e EXCEPT !.id=ToString(<<"recover",1>>)] ELSE e
Lost(a) == [R!ColdOwner(P(a),x.roots[a]) EXCEPT !.up=FALSE,!.resets=1]
Crash ==
 /\ Ready
 /\ x'=[x EXCEPT !.crashed=TRUE,
       !.roots=[a \in Owners |-> Lost(a)],
       !.network=@ \cup {RecoverEvent(a):a \in Owners}]
BeginSteps == UNION {{Transition(t.tag,[x EXCEPT !.roots[a]=t.next,!.network=@ \cup Elements(t.emissions)],<<>>):
 t \in R!Begin(P(a),x.roots[a],a)}:a \in Owners}
LogSteps == {Transition(t.tag,[x EXCEPT !.log=t.next,!.network=@ \cup Elements(t.emissions)],<<>>):t \in D!Actions(DP,x.log)}
Input(e) == IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",[x EXCEPT !.log=t.next,!.network=(@ \ {e}) \cup Elements(t.emissions)],<<>>):t \in D!Receive(DP,x.log,e)}
 ELSE {Transition("input." \o e.kind,[x EXCEPT !.roots[e.dst]=t.next,
 !.network=(@ \ {e}) \cup Elements(t.emissions),
 !.delivered=IF e.kind \in {"journal.deliver","journal.snapshot"} THEN @ \cup {e} ELSE @],<<>>):t \in R!Receive(P(e.dst),x.roots[e.dst],e)}
Candidates == BeginSteps \cup LogSteps \cup UNION {Input(e):e \in x.network}
Service == /\ ~Ready /\ Candidates#{} /\ x'=Choose(Candidates).next
Next == Crash \/ Service \/ UNCHANGED vars
Spec == Init /\ [][Next]_vars /\ WF_vars(Crash) /\ WF_vars(Service)
HasBarriers == \A a \in Owners:\E c \in Elements(x.log.log[a]):c.kind="journal.barrier" /\ c.body.actor=a
BothBarriers == <>HasBarriers
Done == x.crashed /\ \A a \in Owners:x.roots[a].up /\ x.roots[a].roots[a].phase="begun" /\
 a \in x.roots[a].requested /\ x.roots[a].definitions[a]=Definition(a)
Completes == <>Done
DeliverySound == \A e \in x.delivered:D!DeliverySound(x.log.log,e)
NoTwoRecoveries == ~Done
=============================================================================
