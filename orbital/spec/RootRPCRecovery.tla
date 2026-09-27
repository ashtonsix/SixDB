--------------------------- MODULE RootRPCRecovery ---------------------------
EXTENDS Contracts, JournalSchedule
CONSTANTS Bug, Fault, Kind
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog

\* A real R request is one logical submission, but a locally pending transport
\* intent can retransmit that exact envelope. There is no retry counter. The
\* two coalesced channel sets bound representation, not the number of retries.
\* One authored fault is forced at its named causal cut; after it, each network
\* leg, receiver handler, normal finite bootstrap/replay and caller retry is fair.
Sites == {"old","new"}
Data == [t \in {"base","code"}|->IF t="base"
 THEN [kind|->"base",interpretation|->"v1",content|-><<23>>]
 ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
Recipe == [id|->"recipe",base|->"base",code|->"code",patches|-><<>>,interpretation|->"v1"]
Definition(site) == [root|->site,holders|->{1},recipe|->Recipe,cut|->8,context|->"closure-v1",
 generation|->1,requester|->"reader",view|->site,rights|->[read|->{1},write|->{}],
 successor|->IF Kind="custody" /\ site="old" THEN "new" ELSE "",successorOwner|->"new",
 predecessor|->IF site="new" THEN "old" ELSE "",predecessorOwner|->"old"]
P(site) == [roots|->{site},holders|->{1},data|->Data,
 initial|->[h \in {1}|->IF Kind="refuse" /\ site="old" THEN {"base"} ELSE DOMAIN Data],
 reset|->FALSE,gc|->FALSE,abort|->FALSE,bad|->IF Bug="suppress-repeat-reply" THEN Bug ELSE "none",
 retries|->1,owner|->site,actor|->site]
DP == [owners|->Sites,actors|->Sites,subscribers|->[s \in Sites|->{s}],initialConfig|->[s \in Sites|->1]]
RequestKind == CASE Kind="custody" -> "root.custody-query"
 [] Kind="fetch" -> "root.fetch" [] OTHER -> "root.hold"
ReplyKind == CASE Kind="custody" -> "root.custody"
 [] Kind="fetch" -> "root.bytes" [] Kind="refuse" -> "root.refused" [] OTHER -> "root.receipt"
ReplyTag == CASE Kind="custody" -> "send-root-transfer-receipt"
 [] Kind="fetch" -> "serve-retained-recipe" [] Kind="refuse" -> "refuse-missing-recipe"
 [] OTHER -> "holder-durable-receipt"
Receiver == IF Kind="custody" THEN "new" ELSE "old"
EmptyCall == [id|->"",src|->"old",dst|->"new",kind|->"none",body|->[query|->"",recovery|->0,root|->"old"]]
Packet(site,e) == [site|->site,event|->e]
IsCall(e) == e.kind=RequestKind /\ e.body.root="old"
IsReply(e) == e.kind=ReplyKind /\ e.body.root="old"

VARIABLE x
vars == <<x>>
Init == x=[roots|->[s \in Sites|->R!Init(P(s))],log|->D!Init(DP),
 network|->{Packet(s,Event(<<"acquire",s>>,"reader",s,"root.acquire",Definition(s))):
             s \in IF Kind="custody" THEN Sites ELSE {"old"}},
 call|->EmptyCall,pending|->FALSE,requests|->{},replies|->{},faulted|->Fault="none",recovered|->FALSE,
 authored|->{},accepted|->{},retired|->{},outputs|->{},deliveries|->{},retried|->{},repeated|->FALSE,
 beforePeerSent|->{},staleRejected|->FALSE]

Route(s,site,e) ==
 IF IsCall(e) THEN [s EXCEPT !.call=e,!.pending=TRUE,!.requests=@ \cup {e},!.authored=@ \cup {e}]
 ELSE IF IsReply(e) THEN [s EXCEPT !.replies=@ \cup {e},!.outputs=@ \cup {e},
                              !.repeated=@ \/ e \in s.outputs]
 ELSE [s EXCEPT !.network=@ \cup {Packet(site,e)}]
RECURSIVE RouteAll(_,_,_)
RouteAll(s,site,es) == IF es= <<>> THEN s ELSE RouteAll(Route(s,site,Head(es)),site,Tail(es))
AtCut == ~x.faulted /\ CASE Fault="request-loss" -> x.requests#{}
 [] Fault="reply-loss" -> x.replies#{}
 [] Fault="receiver-reset" -> x.roots["new"].custodyRequests#{} /\ x.roots["new"].roots["new"].phase="live"
 [] Fault="sender-reset" -> x.roots["old"].custody#{}
 [] OTHER -> FALSE
FaultStep ==
 /\ AtCut
 /\ IF Fault="request-loss" THEN x'=[x EXCEPT !.faulted=TRUE,!.requests={}]
    ELSE IF Fault="reply-loss" THEN x'=[x EXCEPT !.faulted=TRUE,!.replies={}]
    ELSE LET site==IF Fault="receiver-reset" THEN "new" ELSE "old"
             peer==IF site="new" THEN "old" ELSE "new"
             state==[R!ColdOwner(P(site),x.roots[site]) EXCEPT !.up=FALSE,!.resets=1]
             e==R!RecoveryEvent(P(site),1)
         IN x'=[x EXCEPT !.roots[site]=state,!.faulted=TRUE,!.beforePeerSent=x.roots[peer].sent,
            !.network={p \in @:~(p.site=site /\ p.event.kind="journal.deliver")} \cup {Packet(site,e)},
            !.requests={},!.call=IF site="old" THEN EmptyCall ELSE @,
            !.pending=IF site="old" THEN FALSE ELSE @,
            \* The old reply can arrive after the caller restarts. The durable
            \* peer, including its response history, is untouched by this reset.
            !.replies=IF site="old" THEN @ \cup x.accepted ELSE @]

Retry == /\ ~AtCut /\ Bug#"one-shot" /\ x.pending /\ x.roots["old"].up
 /\ x.call.kind=RequestKind /\ x.call \notin x.requests
 /\ x'=[x EXCEPT !.requests=@ \cup {x.call},!.retried=@ \cup {x.call.id}]
RequestSteps == UNION {{Transition("rpc.receive-request",
 [x EXCEPT !.roots[Receiver]=t.next,!.requests=@ \ {e},
  !.pending=IF Bug="premature-retire" THEN FALSE ELSE @,
  !.retired=IF Bug="premature-retire" THEN @ \cup {e.body.query} ELSE @],<<>>):
 t \in R!Receive(P(Receiver),x.roots[Receiver],e)}:e \in x.requests}
DeliverRequest == /\ ~AtCut /\ RequestSteps#{} /\ x'=Choose(RequestSteps).next
Matches(e) == x.call.kind=RequestKind /\ e.body.root=x.call.body.root /\
 e.body.query=x.call.body.query /\ e.body.recovery=x.call.body.recovery
ReplySteps == UNION {{Transition("rpc.receive-reply",
 [x EXCEPT !.roots["old"]=t.next,!.replies=@ \ {e},
  !.pending=IF Matches(e) THEN FALSE ELSE @,
  !.accepted=IF Matches(e) THEN @ \cup {e} ELSE @,
  !.retired=IF Matches(e) THEN @ \cup {e.body.query} ELSE @,
  !.staleRejected=@ \/ (e.body.recovery<x.roots["old"].resets /\ t.next.custody=x.roots["old"].custody)],<<>>):
 t \in R!Receive(P("old"),x.roots["old"],e)}:e \in x.replies}
DeliverReply == /\ ~AtCut /\ ReplySteps#{} /\ x'=Choose(ReplySteps).next

\* Replies have their own service obligation. An endless retry cannot satisfy
\* fairness for this handler, or for the separate request/reply delivery legs.
AnswerSteps == {Transition(t.tag,RouteAll([x EXCEPT !.roots[Receiver]=t.next],Receiver,t.emissions),<<>>):
 t \in {a \in R!Actions(P(Receiver),x.roots[Receiver]):a.tag=ReplyTag /\
        \E e \in Elements(a.emissions):IsReply(e)}}
Answer == /\ ~AtCut /\ AnswerSteps#{} /\ x'=Choose(AnswerSteps).next
Allowed(t) == t.tag \notin {"copy-immutable-material","root-view-grant","deliver-retained-material"} /\
 (Kind="fetch" \/ t.tag#"send-root-fetch") /\
 ~(t.tag=ReplyTag /\ \E e \in Elements(t.emissions):IsReply(e))
RootSteps == UNION {{Transition(t.tag,RouteAll([x EXCEPT !.roots[site]=t.next],site,t.emissions),<<>>):
 t \in {a \in R!Actions(P(site),x.roots[site]):Allowed(a)}}:site \in Sites}
LogSteps == {Transition(t.tag,[x EXCEPT !.log=t.next,
 !.network=@ \cup {Packet(e.dst,e):e \in Elements(t.emissions)}],<<>>):t \in D!Actions(DP,x.log)}
Input(pkt) == LET e==pkt.event site==pkt.site IN
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal.submit",RouteAll([x EXCEPT !.log=t.next,!.network=@ \ {pkt}],site,t.emissions),<<>>):
       t \in D!Receive(DP,x.log,e)}
 ELSE IF e.kind="RootFailure"
 THEN {Transition("input.chosen-failure-result",[x EXCEPT !.network=@ \ {pkt}],<<>>)}
 ELSE {Transition("input." \o e.kind,
   RouteAll([x EXCEPT !.roots[site]=t.next,!.network=@ \ {pkt},
    !.deliveries=IF e.kind \in {"journal.deliver","journal.snapshot"} THEN @ \cup {e} ELSE @,
    !.recovered=@ \/ e.kind="journal.snapshot"],site,t.emissions),<<>>):t \in R!Receive(P(site),x.roots[site],e)}
Candidates == RootSteps \cup LogSteps \cup UNION {Input(p):p \in x.network}
Service == /\ ~AtCut /\ Candidates#{} /\ x'=Choose(Candidates).next

CurrentEvidence == CASE Kind="custody" -> x.roots["old"].custody
 [] Kind="fetch" -> x.roots["old"].fetched [] Kind="refuse" -> x.roots["old"].refusals
 [] OTHER -> x.roots["old"].receipts
Done == x.faulted /\ ~x.pending /\ x.call.kind=RequestKind /\
 \E b \in CurrentEvidence:b.query=x.call.body.query /\ b.recovery=x.roots["old"].resets
Next == FaultStep \/ Retry \/ DeliverRequest \/ Answer \/ DeliverReply \/ Service \/ UNCHANGED vars
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(FaultStep) /\ WF_vars(Retry) /\ WF_vars(DeliverRequest) /\
 WF_vars(Answer) /\ WF_vars(DeliverReply) /\ WF_vars(Service)
Completes == <>Done
HeldExists == \A s \in Sites:R!HeldExists(P(s),x.roots[s])
LiveRetained == \A s \in Sites:R!LiveRetained(P(s),x.roots[s])
ExactBytes == \A s \in Sites:R!ExactBytes(P(s),x.roots[s])
DeliverySound == \A e \in x.deliveries:D!DeliverySound(x.log.log,e)
RetirementEvidence == x.retired \subseteq {e.body.query:e \in x.accepted}
PendingConservation == x.call.kind=RequestKind /\ x.call.body.query \notin x.retired => x.pending
AcceptedByProtocol == \A e \in x.accepted:
 e.body.recovery=x.roots["old"].resets => e.body \in CurrentEvidence
StableQuery == \A e \in x.requests:e \in x.authored
CapturedIdentity == \A e \in x.authored:
 e.body.root="old" /\ e.body.cut=8 /\ e.body.context="closure-v1" /\ e.body.generation=1
PeerPartition == x.faulted /\ Fault \in {"sender-reset","receiver-reset"} =>
 x.beforePeerSent \subseteq x.roots[IF Fault="sender-reset" THEN "new" ELSE "old"].sent
CurrentCorrelation == \A b \in CurrentEvidence:b.recovery=x.roots["old"].resets
NoRetryJournalRecord == \A site \in Sites:
 Cardinality({i \in 1..Len(x.log.log[site]):x.log.log[site][i].kind \in
  {"root.begin","root.register","root.abort","root.release"}})<=2
ReplyAuthority == \A e \in x.outputs:
 IF Kind="custody" THEN
  /\ e.src="new" /\ e.body.root="old" /\ e.body.successor="new"
  /\ e.body.cut=8 /\ e.body.context="closure-v1" /\ e.body.generation=1
  /\ \E c \in Elements(x.log.log["new"]):c.kind="root.register" /\ c.body=Definition("new")
 ELSE IF Kind="refuse" THEN
  /\ "code" \notin R!Tokens(x.roots["old"],1,DOMAIN x.roots["old"].copies[1])
  /\ \E c \in Elements(x.log.log["old"]):c.kind="root.begin" /\ c.body=Definition("old")
 ELSE x.roots["old"].holds[1]["old"].phase="held" /\
      (IF Kind="fetch" THEN e.body.bytes= <<23>> /\ e.body.recipe=Recipe.id ELSE TRUE)
NoCompletedFault == ~(Done /\ Fault#"none")
NoRepeatReply == ~(Done /\ x.repeated /\ x.retried#{})
NoStaleRejection == ~(Done /\ x.staleRejected)
=============================================================================
