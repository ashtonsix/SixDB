-------------------------- MODULE RootCustodyRecovery --------------------------
EXTENDS Contracts, JournalSchedule
CONSTANTS Bug, FaultOwner
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog
Sites == {"old","new"}
Data == [t \in {"base","code"}|->IF t="base"
 THEN [kind|->"base",interpretation|->"v1",content|-><<23>>]
 ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
Recipe == [id|->"recipe",base|->"base",code|->"code",patches|-><<>>,interpretation|->"v1"]
Definition(site) == [root|->site,holders|->{1},recipe|->Recipe,cut|->8,context|->"closure-v1",
 generation|->1,requester|->"reader",view|->site,rights|->[read|->{1},write|->{}],
 successor|->IF site="old" THEN "new" ELSE "",successorOwner|->"new",
 predecessor|->IF site="new" THEN "old" ELSE "",predecessorOwner|->"old"]
P(site) == [roots|->{site},holders|->{1},data|->Data,initial|->[h \in {1}|->DOMAIN Data],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->"none",retries|->IF FaultOwner="new" /\ site="old" THEN 2 ELSE 1,owner|->site,actor|->site]
DP == [owners|->Sites,actors|->Sites,subscribers|->[s \in Sites|->{s}],initialConfig|->[s \in Sites|->1]]
Packet(site,e) == [site|->IF e.kind \in {"root.custody","root.custody-query"} THEN e.dst ELSE site,event|->e]
Packets(site,es) == {Packet(site,e):e \in Elements(es)}
VARIABLE x
vars == <<x>>
Init == x=[roots|->[s \in Sites|->R!Init(P(s))],log|->D!Init(DP),
 network|->{Packet(s,Event(<<"acquire",s>>,"reader",s,"root.acquire",Definition(s))):s \in Sites},
 closes|->{},crashed|->FALSE,recovered|->FALSE,outputs|->{},oldCustody|->{},freshCustody|->{},
 beforePeerSent|->{},deliveries|->{},custodyOutputs|->{},staleRejected|->FALSE]
Peer == IF FaultOwner="old" THEN "new" ELSE "old"
FreshQuery == ToString(<<"custody-query","old",IF FaultOwner="old" THEN 1 ELSE 0,
                         IF FaultOwner="old" THEN 1 ELSE 2>>)
AtCut == ~x.crashed /\ IF FaultOwner="old"
 THEN x.roots["old"].custody#{} /\ "old" \in x.roots["old"].closed
 ELSE x.roots["new"].custodyRequests#{} /\ x.roots["new"].roots["new"].phase="live"
Crash ==
 /\ AtCut
 /\ LET state==[R!ColdOwner(P(FaultOwner),x.roots[FaultOwner]) EXCEPT !.up=FALSE,!.resets=1]
        e==R!RecoveryEvent(P(FaultOwner),1)
    IN x'=[x EXCEPT !.roots[FaultOwner]=state,!.crashed=TRUE,!.beforePeerSent=x.roots[Peer].sent,
       !.network={p \in @:~(p.site=FaultOwner /\ p.event.kind="journal.deliver")} \cup {Packet(FaultOwner,e)} \cup
        {Packet("old",Event("old-custody-duplicate","new","old","root.custody",b)):b \in x.oldCustody}]
Close == /\ x.roots["old"].up /\ x.roots["old"].resets \notin x.closes
 /\ \E e \in x.outputs:e.kind="Material" /\ e.body.root="old"
 /\ x'=[x EXCEPT !.closes=@ \cup {x.roots["old"].resets},!.network=@ \cup
    {Packet("old",Event(<<"close",x.roots["old"].resets>>,"reader","old","root.close",[root|->"old"]))}]
RootSteps == UNION {{Transition(t.tag,[x EXCEPT !.roots[site]=t.next,
 !.network=@ \cup Packets(site,t.emissions),
 !.custodyOutputs=@ \cup {e \in Elements(t.emissions):e.kind="root.custody"}],<<>>):
 t \in {a \in R!Actions(P(site),x.roots[site]):a.tag#"copy-immutable-material" /\
 ~(Bug="no-requery" /\ site="old" /\ x.crashed /\ a.tag="send-root-transfer-query") /\
 \* One ordinary retry is authored after the peer loss, not inferred from a
 \* local authoritative view of that peer's state. No readiness rule reads it.
 ~(FaultOwner="new" /\ ~x.recovered /\ a.tag="send-root-transfer-query" /\
   \E e \in Elements(a.emissions):e.body.query=FreshQuery)}}:site \in Sites}
LogSteps == {Transition(t.tag,[x EXCEPT !.log=t.next,
 !.network=@ \cup {Packet(e.dst,e):e \in Elements(t.emissions)}],<<>>):t \in D!Actions(DP,x.log)}
Input(pkt) == LET e==pkt.event site==pkt.site IN
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",[x EXCEPT !.log=t.next,!.network=(@ \ {pkt}) \cup Packets(site,t.emissions)],<<>>):t \in D!Receive(DP,x.log,e)}
 ELSE IF e.kind \in {"Material","ViewGrant","RootFailure"}
 THEN {Transition("input.output",[x EXCEPT !.network=@ \ {pkt},!.outputs=@ \cup {e}],<<>>)}
 ELSE {Transition("input." \o e.kind,[x EXCEPT !.roots[site]=t.next,!.network=(@ \ {pkt}) \cup Packets(site,t.emissions),
 !.deliveries=IF e.kind \in {"journal.deliver","journal.snapshot"} THEN @ \cup {e} ELSE @,
 !.recovered=@ \/ (site=FaultOwner /\ e.kind="journal.snapshot"),
 !.oldCustody=IF e.kind="root.custody" THEN IF e.body.recovery=0 THEN @ \cup {e.body} ELSE @ ELSE @,
 !.freshCustody=IF e.kind="root.custody" THEN IF e.body.query=FreshQuery THEN @ \cup {e.body} ELSE @ ELSE @,
 !.staleRejected=IF e.kind="root.custody" THEN IF x.crashed /\ e.body.recovery=0
    THEN @ \/ t.next.custody=x.roots[site].custody ELSE @ ELSE @],<<>>):t \in R!Receive(P(site),x.roots[site],e)}
Candidates == RootSteps \cup LogSteps \cup UNION {Input(p):p \in x.network}
Service == /\ ~AtCut /\ Candidates#{} /\ x'=Choose(Candidates).next
Done == x.crashed /\ x.recovered /\ x.roots["old"].roots["old"].phase="released"
Next == Crash \/ Close \/ Service \/ UNCHANGED vars
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Crash) /\ WF_vars(Close) /\ WF_vars(Service)
Completes == <>Done
HeldExists == \A s \in Sites:R!HeldExists(P(s),x.roots[s])
LiveRetained == \A s \in Sites:R!LiveRetained(P(s),x.roots[s])
ExactBytes == \A s \in Sites:R!ExactBytes(P(s),x.roots[s])
DeliverySound == \A e \in x.deliveries:D!DeliverySound(x.log.log,e)
PeerPartition == x.crashed => x.beforePeerSent \subseteq x.roots[Peer].sent
CurrentEvidence == \A b \in x.roots["old"].custody:b.recovery=x.roots["old"].resets
CustodyAuthority == \A e \in x.custodyOutputs:
 /\ e.src="new" /\ e.body.root="old" /\ e.body.successor="new"
 /\ e.body.cut=8 /\ e.body.context="closure-v1" /\ e.body.generation=1
 /\ \E c \in Elements(x.log.log["new"]):c.kind="root.register" /\ c.body=Definition("new")
RetirementEvidence == Done =>
 /\ x.freshCustody#{} /\ (IF FaultOwner="old" THEN 1 ELSE 0) \in x.closes
 /\ x.roots["new"].holds[1]["new"].phase="held"
 /\ R!M!Reconstruct(R!HeldData(x.roots["new"],1,"new"),Recipe)= <<23>>
NoFreshCustody == ~(Done /\ x.freshCustody#{})
NoStaleCustody == ~x.staleRejected
=============================================================================
