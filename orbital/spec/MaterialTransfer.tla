--------------------------- MODULE MaterialTransfer ---------------------------
EXTENDS Contracts, Integers
CONSTANTS Bug, ResetDuringCopy, LoseReceipt, Convert, DropKind
R == INSTANCE RecoveryKernel
J == INSTANCE DurableLog
M == INSTANCE MaterialCore
Sites == {"old","new"}
Data == [t \in {"base","code","patch","flat"} |->
 CASE t="base" -> [kind |-> "base",interpretation |-> "v1",content |-> <<2,4>>]
   [] t="code" -> [kind |-> "code",interpretation |-> "v1",content |-> "replace-byte-v1"]
   [] t="flat" -> [kind |-> "base",interpretation |-> "v1",content |-> <<9,4>>]
   [] OTHER -> [kind |-> "patch",interpretation |-> "v1",content |-> [index |-> 1,value |-> 9]]]
Recipe == [id |-> "snapshot-and-tail",base |-> "base",code |-> "code",patches |-> <<"patch">>,interpretation |-> "v1"]
NewRecipe == IF Convert THEN [Recipe EXCEPT !.id="flat-snapshot",!.base="flat",!.patches= <<>>] ELSE Recipe
TransferTokens == M!Needed(NewRecipe)
Request(site) == [root |-> site,holders |-> {1},recipe |-> IF site="old" THEN Recipe ELSE NewRecipe,cut |-> 3,
 context |-> "captured",generation |-> 1,requester |-> "reader",view |-> site,
 rights |-> [read |-> {1,2},write |-> {}],waitForMaterial |-> TRUE,
 successor |-> IF site="old" THEN "new" ELSE "",
 predecessor |-> IF site="new" THEN "old" ELSE "",predecessorOwner |-> "old"]
P(site) == [roots |-> {site},holders |-> {1},requests |-> [r \in {site} |-> Request(site)],
 data |-> Data,initial |-> [h \in {1} |-> IF site="old" THEN M!Needed(Recipe) ELSE {}],
 reset |-> TRUE,gc |-> FALSE,abort |-> FALSE,bad |-> Bug,retries |-> 2,owner |-> site,actor |-> site]
JP == [owners |-> Sites,actors |-> Sites,subscribers |-> [o \in Sites |-> {o}],
 initialConfig |-> [o \in Sites |-> 1]]
Packet(site,e) == [site |-> IF e.kind="root.custody" THEN e.dst ELSE site,event |-> e]
Packets(site,es) == {Packet(site,e):e \in Elements(es)}
VARIABLES states,journal,network,phase,copied,observed,dropped,resetSeen,joinedWhileIncomplete
vars == <<states,journal,network,phase,copied,observed,dropped,resetSeen,joinedWhileIncomplete>>
Init == /\ states=[site \in Sites |-> R!Init(P(site))] /\ journal=J!Init(JP)
 /\ network={Packet("old",Event("start","reader","old","root.acquire",Request("old")))}
 /\ phase=0 /\ copied={} /\ observed={} /\ dropped=FALSE /\ resetSeen=FALSE /\ joinedWhileIncomplete=FALSE
Join == /\ phase=0 /\ "old" \in observed
 /\ phase'=1 /\ network'=network \cup
    {Packet("new",Event("join","reader","new","root.acquire",Request("new"))),
     Packet("old",Event("close-old","reader","old","root.close",[root |-> "old"]))}
 /\ UNCHANGED <<states,journal,copied,observed,dropped,resetSeen,joinedWhileIncomplete>>
\* The transfer reads the actual old holder's retained bytes. The receiver starts
\* with no material and journals Begin before fetching the snapshot and tail.
Copy(t) == /\ phase=1 /\ states["new"].roots["new"].phase="begun"
 /\ t \notin copied
 /\ t \in TransferTokens
 /\ (t#"patch" \/ {"base","code"} \subseteq copied)
 /\ LET source == R!HeldData(states["old"],1,"old")
         value == IF t="flat" THEN [kind |-> "base",interpretation |-> "v1",content |-> M!Reconstruct(source,Recipe)]
                  ELSE source[t]
         fresh == R!Copy(1,t,2,value)
     IN network'=network \cup {Packet("new",Event(<<"copy",t>>,"old","new","root.import",[holder |-> 1,copy |-> fresh]))}
 /\ copied'=copied \cup {t}
 /\ joinedWhileIncomplete'=TRUE
 /\ UNCHANGED <<states,journal,phase,observed,dropped,resetSeen>>
PendingTail == \E q \in DOMAIN states["new"].registry[1].operations:
 LET op == states["new"].registry[1].operations[q]
 IN op.request.kind="copy" /\ op.request.payload.copy.token=(IF Convert THEN "flat" ELSE "patch") /\ op.phase="active"
Reset == /\ ResetDuringCopy /\ ~resetSeen /\ PendingTail
 /\ \E tr \in R!HolderRestart(P("new"),states["new"],1):
     /\ states'=[states EXCEPT !["new"]=tr.next]
     /\ network'=network \cup Packets("new",tr.emissions)
 /\ resetSeen'=TRUE /\ UNCHANGED <<journal,phase,copied,observed,dropped,joinedWhileIncomplete>>
Drop == /\ LoseReceipt /\ ~dropped
 /\ \E pkt \in network:
    /\ pkt.event.kind=DropKind /\ (DropKind="root.custody" \/ pkt.site="new")
    /\ network'=network \ {pkt}
 /\ dropped'=TRUE /\ UNCHANGED <<states,journal,phase,copied,observed,resetSeen,joinedWhileIncomplete>>
Local(site) ==
 LET s == states[site]
     moves == {tr \in R!Actions(P(site),s):
       /\ tr.tag \notin {"root-owner-reset","holder-process-reset"}
       /\ (tr.tag#"send-root-transfer-receipt" \/ DropKind#"root.custody" \/ ~LoseReceipt \/ dropped \/
             ToString(<<"custody",site,1>>) \notin s.sent)
       /\ (~ResetDuringCopy \/ site#"new" \/ resetSeen \/ ~PendingTail \/ tr.tag#"backend-complete")}
 IN { [site |-> site,move |-> tr]:tr \in moves }
Locals == UNION {Local(site):site \in Sites}
LocalRank(tag) == CASE tag="root-propose" -> 0 [] tag="send-root-hold" -> 1
 [] tag="holder-submit" -> 2 [] tag="backend-start" -> 3 [] tag="backend-complete" -> 4
 [] tag="backend-retire" -> 5 [] tag="registry-drained" -> 6 [] tag="holder-recovered" -> 7
 [] tag="holder-durable-receipt" -> 8 [] tag="root-view-grant" -> 9
 [] tag="serve-retained-recipe" -> 10 [] tag="deliver-retained-material" -> 11
 [] tag="send-root-transfer-receipt" -> 12 [] OTHER -> 13
SelectedLocals == {t \in Locals:~(\E u \in Locals:LocalRank(u.move.tag)<LocalRank(t.move.tag))}
LocalStep == \E item \in SelectedLocals:
 /\ states'=[states EXCEPT ![item.site]=item.move.next]
 /\ network'=network \cup Packets(item.site,item.move.emissions)
 /\ UNCHANGED <<journal,phase,copied,observed,dropped,resetSeen,joinedWhileIncomplete>>
Deliverable == {pkt \in network:
 LET e == pkt.event IN
 IF e.kind \in {"journal.submit","journal.recover"} THEN J!Receive(JP,journal,e)#{}
 ELSE IF e.kind \in {"Material","ViewGrant"} THEN TRUE
 ELSE IF e.kind=DropKind /\ LoseReceipt /\ ~dropped /\ (DropKind="root.custody" \/ pkt.site="new") THEN FALSE
 ELSE R!Receive(P(pkt.site),states[pkt.site],e)#{}}
Deliver == \E pkt \in Deliverable:
 LET e == pkt.event IN
 /\ network'=(network \ {pkt}) \cup
       (IF e.kind \in {"journal.submit","journal.recover"}
        THEN {Packet(trEvent.body.owner,trEvent):trEvent \in Elements((CHOOSE tr \in J!Receive(JP,journal,e):TRUE).emissions)}
        ELSE IF e.kind \in {"Material","ViewGrant"} THEN {}
        ELSE Packets(pkt.site,(CHOOSE tr \in R!Receive(P(pkt.site),states[pkt.site],e):TRUE).emissions))
 /\ journal'=(IF e.kind \in {"journal.submit","journal.recover"} THEN (CHOOSE tr \in J!Receive(JP,journal,e):TRUE).next ELSE journal)
 /\ states'=(IF e.kind \in {"journal.submit","journal.recover","Material","ViewGrant"} THEN states
             ELSE [states EXCEPT ![pkt.site]=(CHOOSE tr \in R!Receive(P(pkt.site),states[pkt.site],e):TRUE).next])
 /\ observed'=(IF e.kind="Material" THEN observed \cup {e.body.root} ELSE observed)
 /\ UNCHANGED <<phase,copied,dropped,resetSeen,joinedWhileIncomplete>>
Journal == \E tr \in J!Actions(JP,journal):
 /\ journal'=tr.next
 /\ network'=network \cup {Packet(e.body.owner,e):e \in Elements(tr.emissions)}
 /\ UNCHANGED <<states,phase,copied,observed,dropped,resetSeen,joinedWhileIncomplete>>
Collect == /\ states["old"].holds[1]["old"].phase="terminal"
 /\ \E id \in DOMAIN states["old"].copies[1]:
    \E tr \in R!GC([P("old") EXCEPT !.gc=TRUE],states["old"],1,id):
     /\ states'=[states EXCEPT !["old"]=tr.next]
     /\ network'=network \cup Packets("old",tr.emissions)
 /\ UNCHANGED <<journal,phase,copied,observed,dropped,resetSeen,joinedWhileIncomplete>>
Done == /\ "new" \in observed /\ states["new"].roots["new"].phase="live"
 /\ states["old"].roots["old"].phase="released"
 /\ states["old"].holds[1]["old"].phase="terminal"
 /\ DOMAIN states["old"].copies[1]={}
 /\ \A site \in Sites:R!V!RegistryDebt(states[site].registry[1])={}
 /\ (~LoseReceipt \/ dropped) /\ (~ResetDuringCopy \/ resetSeen)
\* An authored causal history: messages and journal work drain at each cut;
\* disk operations remain real queued/active/completed/retired registry entries.
Next == IF ENABLED Reset THEN Reset ELSE IF ENABLED Drop THEN Drop
 ELSE IF Deliverable#{} THEN Deliver ELSE IF J!Actions(JP,journal)#{} THEN Journal
 ELSE IF ENABLED Join THEN Join ELSE IF Locals#{} THEN LocalStep
 ELSE (\E t \in DOMAIN Data:Copy(t)) \/ Collect \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)
HeldExists == \A site \in Sites:R!HeldExists(P(site),states[site])
LiveRetained == \A site \in Sites:R!LiveRetained(P(site),states[site])
ExactBytes == \A site \in Sites:R!ExactBytes(P(site),states[site])
TransferBeforeRelease == states["old"].roots["old"].phase="released" => states["new"].roots["new"].phase="live"
Completes == <>Done
NoCompletedTransfer == ~Done
NoLateCopy == ~(resetSeen /\ states["new"].lateWrites /\ Done)
NoLostReceiptRecovery == ~(dropped /\ Done)
NoIncompleteJoin == ~(joinedWhileIncomplete /\ Done)
=============================================================================
