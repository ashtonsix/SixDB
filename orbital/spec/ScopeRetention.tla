----------------------------- MODULE ScopeRetention -----------------------------
EXTENDS Contracts, Integers
CONSTANTS JournalNone, TxNone, Bug
VARIABLES scope,root,journal,network,started,closedEarly,observed,definition,otherWork,grantSeen
vars == <<scope,root,journal,network,started,closedEarly,observed,definition,otherWork,grantSeen>>
S == INSTANCE ScopeReopening WITH run<-scope, Bug<-"none", RestartDestination<-TRUE,
 StaleTiming<-"after",RouteTarget<-2
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog
M == INSTANCE MaterialCore
Recipe == [id|->"old-cut",base|->"value",code|->"decoder",patches|-> <<>>,interpretation|->"v1"]
ExpectedData == [t \in {"value","decoder"}|->IF t="value"
 THEN [kind|->"base",interpretation|->"v1",content|-> <<1>>]
 ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
P == [roots|->{"old-cut"},holders|->{1},data|->ExpectedData,initial|->[h \in {1}|->{}],
 reset|->FALSE,gc|->TRUE,abort|->FALSE,bad|->"none",retries|->1,owner|->"retention",actor|->"retention"]
DP == [owners|->{"retention"},actors|->{"retention"},subscribers|->[o \in {"retention"}|->{"retention"}],
 initialConfig|->[o \in {"retention"}|->0]]
Init == /\ S!Init /\ root=R!Init(P) /\ journal=D!Init(DP) /\ network={}
 /\ started=FALSE /\ closedEarly=FALSE /\ observed={} /\ definition=[dummy|->0] /\ otherWork=FALSE /\ grantSeen=FALSE
BoundInstalled == scope.source.readCut[2][1]>0
Acquire ==
 /\ ~started /\ BoundInstalled
 /\ LET cut==scope.source.readCut[2][1]
        vs=={v \in scope.source.versions:v.key=1 /\ v.position<=cut}
        version==CHOOSE v \in vs:\A w \in vs:v.position>=w.position
        request==[root|->"old-cut",holders|->{1},recipe|->Recipe,cut|->cut,
          context|->S!T!Context(S!Old,scope.source,2),generation|->1,requester|->"historical-reader",
          view|->"old-cut-view",rights|->[read|->{1},write|->{}],waitForMaterial|->TRUE,successor|->""]
        body==[kind|->"base",interpretation|->"v1",content|-> <<version.value>>]
    IN /\ definition'=request
       /\ network'=network \cup {
         Event("retain-old-cut","historical-reader","retention","root.acquire",request),
         Event("import-old-value",S!T!Fold(1),"retention","root.import",
            [holder|->1,copy|->R!Copy(1,"value",1,body)]),
         Event("import-old-code",S!T!Fold(1),"retention","root.import",
            [holder|->1,copy|->R!Copy(1,"decoder",1,ExpectedData["decoder"])])}
 /\ started'=TRUE /\ UNCHANGED <<scope,root,journal,closedEarly,observed,otherWork,grantSeen>>

(* The map transfer does not release an independent history-reader obligation.
   The old retention owner keeps serving it. The mutation deliberately makes
   that incorrect release through real root journal and holder-delete actions. *)
EarlyClose ==
 /\ Bug="drop-retention" /\ started /\ ~closedEarly /\ scope.destination.activeMap=2
 /\ network'=network \cup {Event("incorrect-map-release","map-adapter","retention","root.close",[root|->"old-cut"])}
 /\ closedEarly'=TRUE /\ UNCHANGED <<scope,root,journal,started,observed,definition,otherWork,grantSeen>>
RSteps == {Transition(tr.tag,[root|->tr.next,journal|->journal,
 network|->network \cup Elements(tr.emissions),observed|->observed,granted|->grantSeen],<<>>):
 tr \in {x \in R!Actions(P,root):
   (x.tag#"serve-retained-recipe" \/ S!Done) /\
   (x.tag#"begin-physical-delete" \/ root.roots["old-cut"].phase="released")}}
DSteps == {Transition(tr.tag,[root|->root,journal|->tr.next,
 network|->network \cup Elements(tr.emissions),observed|->observed,granted|->grantSeen],<<>>):tr \in D!Actions(DP,journal)}
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input",[root|->root,journal|->tr.next,network|->(network \ {e}) \cup Elements(tr.emissions),observed|->observed,granted|->grantSeen],<<>>):
       tr \in D!Receive(DP,journal,e)}
 ELSE IF e.kind \in {"Material","ViewGrant","RootFailure"}
 THEN {Transition("input",[root|->root,journal|->journal,network|->network \ {e},
      observed|->IF e.kind="Material" THEN observed \cup {e.body} ELSE observed,
      granted|->grantSeen \/ (e.kind="ViewGrant" /\ e.body.root="old-cut" /\
        e.body.context=definition.context /\ e.body.c=definition.cut)],<<>>)}
 ELSE {Transition("input",[root|->tr.next,journal|->journal,network|->(network \ {e}) \cup Elements(tr.emissions),observed|->observed,granted|->grantSeen],<<>>):
       tr \in R!Receive(P,root,e)}
Inputs == UNION {Input(e):e \in network}
Rank(tag) == CASE tag="input" -> 0 [] tag="log.deliver" -> 1 [] tag="log.choose" -> 2
 [] tag="root-propose" -> 3 [] tag="send-root-hold" -> 4 [] tag="holder-submit" -> 5
 [] tag="backend-start" -> 6 [] tag="backend-complete" -> 7 [] tag="backend-retire" -> 8
 [] tag="holder-durable-receipt" -> 9 [] tag="root-view-grant" -> 10
 [] tag="serve-retained-recipe" -> 11 [] tag="deliver-retained-material" -> 12 [] OTHER -> 13
Candidates == {tr \in RSteps \cup DSteps \cup Inputs:
 tr.next#[root|->root,journal|->journal,network|->network,observed|->observed,granted|->grantSeen]}
RootStep ==
 /\ Candidates#{}
 /\ LET tr==CHOOSE x \in Candidates:\A y \in Candidates:Rank(x.tag)<=Rank(y.tag)
    IN /\ root'=tr.next.root /\ journal'=tr.next.journal /\ network'=tr.next.network /\ observed'=tr.next.observed /\ grantSeen'=tr.next.granted
 /\ UNCHANGED <<scope,started,closedEarly,definition,otherWork>>
ScopeStep == /\ (~BoundInstalled \/ grantSeen) /\ S!Next /\ UNCHANGED <<root,journal,network,started,closedEarly,observed,definition,otherWork,grantSeen>>
Done == S!Done /\ observed#{}
Other == S!Done /\ otherWork'=~otherWork /\ UNCHANGED <<scope,root,journal,network,started,closedEarly,observed,definition,grantSeen>>
Next == IF ENABLED Acquire THEN Acquire ELSE IF ENABLED EarlyClose THEN EarlyClose
 ELSE IF Candidates#{} THEN RootStep ELSE ScopeStep \/ Other
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)
Completes == <>Done
HeldExists == R!HeldExists(P,root)
LiveRetained == R!LiveRetained(P,root)
SourceAuthority == S!JournalAgreement /\ S!JournalEvidence /\ S!ImportProvenance /\ S!ActivationEvidence
MapOutcomes == S!NewOrdering /\ S!NewObservation /\ S!NewOutcome /\ S!StaleFence /\ S!StaleBeginFence
BoundProvenance == started => \E h \in scope.journal.chosenHistory:
 \E c \in Elements(h.seq):c.kind="bound" /\ c.body.tx=2 /\ c.body.key=1 /\
 c.body.data.cut=definition.cut /\ c.body.data.context=definition.context
OldCutMaterial == \A b \in observed:
 b.bytes= <<1>> /\ b.root="old-cut" /\ b.recipe=Recipe.id /\ definition.cut=scope.savedFloor /\
 scope.destination.tx.outcome[3].result=2 /\ root.roots["old-cut"].phase="live"
PhysicalClosure == S!Done => M!WellTyped(R!Data(root,1,DOMAIN root.copies[1]),Recipe)
(* ALIAS is diagnostic rendering only: unlike VIEW it does not change state
   fingerprints, transitions, invariants or the temporal graph. Full concrete
   provider/transaction state remains in vars and the model checker. *)
RetentionTrace == [sourceIndex|->scope.source.journalIndex,sourceClosed|->~scope.source.mapOpen,
 sourceBounds|->scope.source.bounds,targetMap|->scope.destination.activeMap,
 targetIndex|->scope.destination.tx.journalIndex,targetPublished|->scope.destination.tx.published,
 targetBounds|->scope.destination.tx.bounds,targetVersions|->scope.destination.tx.versions,
 rootState|->root.roots["old-cut"],holder|->root.holds[1],copies|->root.copies[1],
 viewGranted|->grantSeen,request|->definition,pending|->network,
 retainedRead|->observed,scopeDone|->S!Done,done|->Done,unrelated|->otherWork]
NoRetainedRead == ~Done
=============================================================================
