---------------------------- MODULE DeliveryRetention ----------------------------
EXTENDS Contracts, Integers
CONSTANTS Bug, Cancel, LateJoin
D == INSTANCE DeliveryKernel
R == INSTANCE RecoveryKernel
J == INSTANCE DurableLog
Context == <<"lineage",4,"program-v1">>
Recipients == IF LateJoin THEN {4,5,6} ELSE {4,5}
Roots == IF LateJoin THEN {"source","late"} ELSE {"source"}
Actor(a) == <<"actor",a>>
Data == [t \in {"base","code"}|->IF t="base"
 THEN [kind|->"base",interpretation|->"v1",content|-><<6>>]
 ELSE [kind|->"code",interpretation|->"v1",content|->"replace-byte-v1"]]
Recipe == [id|->"output",base|->"base",code|->"code",patches|-><<>>,interpretation|->"v1"]
Definition(r) ==
 [root|->r,holders|->{11},recipe|->Recipe,cut|->4,context|->Context,generation|->1,
  requester|->Actor(3),view|->r,rights|->[read|->{1},write|->{}],
  successor|->IF r="source" /\ LateJoin THEN "late" ELSE "",
  predecessor|->IF r="late" THEN "source" ELSE "",predecessorOwner|->Actor(7)]
RP == [roots|->Roots,holders|->{11},data|->Data,initial|->[h \in {11}|->DOMAIN Data],
 reset|->FALSE,gc|->TRUE,abort|->FALSE,bad|->"none",retries|->1,owner|->7,actor|->Actor(7)]
DP == [keys|->{1},baseKeys|->{1},child|->2,parent|->1,origins|->{1},recipients|->Recipients,
 initialMembers|->{4,5},inputs|->[o \in {1}|->[k \in {1}|->3]],expected|->[k \in {1}|->6],
 lineage|->"lineage",call|->"result",cut|->4,interpretation|->"v1",context|->Context,
 forms|->{"raw"},routes|->1,duplicates|->1,dynamic|->FALSE,cancelRecipients|->{4},
 reset|->FALSE,cancel|->Cancel,join|->LateJoin,bad|->IF Bug="transport-release" THEN "transport-completion" ELSE "none",
 owner|->3,externalOrigins|->TRUE]
Owners == {3,7} \cup Recipients
JP == [owners|->Owners,actors|->{Actor(a):a \in Owners},
 subscribers|->[a \in Owners|->{Actor(a)}],initialConfig|->[a \in Owners|->1]]
VARIABLE run
vars == <<run>>
Init == run=[delivery|->D!Init(DP),roots|->R!Init(RP),journal|->J!Init(JP),
 network|->{Event("acquire",Actor(3),Actor(7),"root.acquire",Definition("source"))},
 grants|->{},materials|->{},origin|->FALSE,releases|->{},cancelled|->FALSE]
CloseRoot(r) == Event(<<"close",r>>,Actor(3),Actor(7),"root.close",[root|->r])
Input(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal",[run EXCEPT !.journal=tr.next,
        !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,run.journal,e)}
 ELSE IF e.kind="ViewGrant"
 THEN {Transition("input.grant",[run EXCEPT !.grants=@ \cup {e.body},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="Material"
 THEN {Transition("input.material",[run EXCEPT !.materials=@ \cup {e.body},!.network=@ \ {e}],<<>>)}
 ELSE IF e.kind="delivery.release"
 THEN {Transition("input.release",[run EXCEPT !.releases=@ \cup {e.body},
       !.network=(@ \ {e}) \cup {CloseRoot("source")} \cup
       (IF LateJoin THEN {Event("late-acquire",Actor(3),Actor(7),"root.acquire",Definition("late"))} ELSE {})],<<>>)}
 ELSE IF e.kind="delivery.join-release"
 THEN {Transition("input.join-release",[run EXCEPT !.network=(@ \ {e}) \cup {CloseRoot("late")}],<<>>)}
 ELSE IF e.kind="delivery.cancelled"
 THEN {Transition("input.cancelled",[run EXCEPT !.cancelled=TRUE,
       !.network=(@ \ {e}) \cup (IF Bug="cancel-release" THEN {CloseRoot("source")} ELSE {})],<<>>)}
 ELSE IF (e.kind \in {"journal.deliver","journal.snapshot"} /\ e.body.owner#7) \/
         e.kind \in {"delivery.payload","delivery.transport","delivery.processed","delivery.derived"}
 THEN {Transition("input.delivery",[run EXCEPT !.delivery=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in D!Receive(DP,run.delivery,e)}
 ELSE {Transition("input.root",[run EXCEPT !.roots=tr.next,
       !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in R!Receive(RP,run.roots,e)}
EligibleInputs == {e \in run.network:Input(e)#{}}
Kind(tr) == IF tr.emissions= <<>> THEN "" ELSE
 IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE ""
LocalDelivery == {tr \in D!Actions(DP,run.delivery):
 /\ (Kind(tr)#"delivery.join" \/ (1 \in run.delivery.closed /\
       (\E g \in run.grants:g.root="late") /\ (\E m \in run.materials:m.root="late")))
 /\ (Kind(tr)#"delivery.cancel" \/ DOMAIN run.delivery.custody[4]#{})}
RootActions == {tr \in R!Actions(RP,run.roots):
 tr.tag#"begin-physical-delete" \/ run.grants#{}}
Origin ==
 /\ ~run.origin
 /\ \E g \in run.grants: \E m \in run.materials:
    /\ g.root="source" /\ m.root=g.root /\ g.recipe=m.recipe
    /\ run'=[run EXCEPT !.origin=TRUE,!.network=@ \cup
         {Event("derived",1,3,"delivery.derived",[origin|->1,key|->1,id|->D!Logical(DP,1),
             context|->g.context,value|->m.bytes[1]])}]
Service ==
 IF EligibleInputs#{} THEN
   LET e==CHOOSE e \in EligibleInputs:TRUE
       tr==CHOOSE tr \in Input(e):TRUE IN run'=tr.next
 ELSE IF J!Actions(JP,run.journal)#{} THEN
   LET tr==CHOOSE tr \in J!Actions(JP,run.journal):TRUE
   IN run'=[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE IF RootActions#{} THEN
   LET tr==CHOOSE tr \in RootActions:TRUE
   IN run'=[run EXCEPT !.roots=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE LET normal=={tr \in LocalDelivery:Kind(tr)#"delivery.cancel"} IN
   /\ normal#{}
   /\ LET closing=={tr \in normal:tr.tag="discharge-output-obligation"}
          tr==CHOOSE tr \in (IF closing#{} THEN closing ELSE normal):TRUE
      IN run'=[run EXCEPT !.delivery=tr.next,!.network=@ \cup Elements(tr.emissions)]
CancelStep == \E tr \in {tr \in LocalDelivery:Kind(tr)="delivery.cancel"}:
 run'=[run EXCEPT !.delivery=tr.next,!.network=@ \cup Elements(tr.emissions)]
Done == /\ 1 \in run.delivery.closed /\ (~LateJoin \/ 6 \in run.delivery.joinedComplete)
        /\ \A r \in Roots:run.roots.roots[r].phase="released"
        /\ DOMAIN run.roots.copies[11]={}
Next == Service \/ Origin \/ CancelStep \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Service) /\ WF_vars(Origin) /\ WF_vars(CancelStep)
Completes == <>Done
Pending == run.delivery.outbox[1].open /\
 \E r \in run.delivery.members \ run.delivery.cancelled:r \notin run.delivery.done[1]
RetainedWhileNeeded == Pending => \E r \in Roots:run.roots.roots[r].phase="live"
PhysicalClosure == R!HeldExists(RP,run.roots) /\ R!LiveRetained(RP,run.roots)
ExactOrigin == \A b \in run.delivery.candidates:b.value=6
EffectMultiplicity == D!EffectMultiplicity(DP,run.delivery)
JoinedCoverage == D!JoinedCoverage(DP,run.delivery)
NoCollected == ~Done
NoLateDebt == ~(6 \in run.delivery.members /\ 6 \notin run.delivery.done[1] /\
 run.roots.roots["source"].phase="released" /\ run.roots.roots["late"].phase="live")
NoSharedCancel == ~(run.cancelled /\ 5 \notin run.delivery.done[1] /\
 run.roots.roots["source"].phase="live")
=============================================================================
