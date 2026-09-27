--------------------------- MODULE PendingCutTransfer ---------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS TxNone, Bug, Result, Crossing
T == INSTANCE TxKernel
C == INSTANCE CutMaterialKernel
R == INSTANCE RecoveryKernel
D == INSTANCE DurableLog
M == INSTANCE MaterialCore
Base == Params("chain","none",FALSE)
P == [Base EXCEPT !.transactions={1,2,3},!.keys={1},!.material=TRUE,
 !.writes=[t \in {1,2,3}|->{1}],!.reads=[t \in {1,2,3}|->IF t=2 THEN {1} ELSE {}],
 !.program[1]=IF Result="value" THEN "put" ELSE "none",!.value[1]=7]
CP == [shards|->{1},transactions|->{1,2,3},keys|->{1},home|->[k \in {1}|->1],initial|->[k \in {1}|->0],gc|->TRUE,bad|->"none"]
Sites == {"old","new"}
Control(site) == site \o "-control"
Fallback == C!Token(1,0)
InitialData == (Fallback :> C!Datum(0)) @@ (C!Code :> C!CodeDatum)
Roots(site) == IF site="old" THEN {"old"} ELSE {"new","future","head"}
RP(site) == [roots|->Roots(site),holders|->{1},data|->InitialData,
 initial|->[h \in {1}|->IF site="old" THEN DOMAIN InitialData ELSE {}],reset|->FALSE,gc|->FALSE,
 abort|->FALSE,bad|->"none",retries|->1,owner|->site,actor|->site]
Owners == Sites \cup {Control(site):site \in Sites}
DP == [owners|->Owners,actors|->Owners,subscribers|->[o \in Owners|->{o}],initialConfig|->[o \in Owners|->0]]
EmptyMaterial == [C!Init(CP) EXCEPT !.data=[id \in {}|->id]]
Tagged(c) == [a|->c.kind] @@ c
Wire(e) == [a|->ToString(<<e.kind,IF e.kind="tx.fact" THEN e.body.kind ELSE "">>)] @@
 (IF e.kind="journal.submit" THEN [e EXCEPT !.body=Tagged(@)] ELSE e)
Events(es) == {Wire(es[i]):i \in 1..Len(es)}
CutEvents(es) == SelectSeq(es,LAMBDA e:e.kind \in {"root.source","root.retain-cut"})
Forward(es) == {e \in Events(es):e.kind \notin {"root.source","root.retain-cut","tx.fact"}}
RECURSIVE Facts(_,_)
Facts(s,es) == IF es= <<>> THEN s ELSE
 LET e==Head(es) ts==IF e.kind="tx.fact" THEN T!Receive(P,s,e) ELSE {}
 IN Facts(IF ts={} THEN s ELSE (CHOOSE z \in ts:TRUE).next,Tail(es))
Submit(owner,id,kind,body) == Wire(Event(id,owner,owner,"journal.submit",Command(owner,id,kind,body)))
Packet(site,e) == [site|->IF e.kind="root.custody" THEN e.dst ELSE site,event|->Wire(e)]
Packets(site,es) == {Packet(site,es[i]):i \in 1..Len(es)}
Request(site,b) == [root|->site,holders|->{1},recipe|->C!Recipe(1,0),cut|->b.cut,context|->b.context,
 generation|->1,requester|->site,view|->site,rights|->[read|->{1},write|->{}],waitForMaterial|->TRUE,
 successor|->IF site="old" THEN "new" ELSE "",predecessor|->IF site="new" THEN "old" ELSE "",predecessorOwner|->"old"]
VARIABLE x
vars == <<x>>
Init == x=[tx|->T!Init(P),old|->C!Init(CP),current|->EmptyMaterial,roots|->[s \in Sites|->R!Init(RP(s))],
 journal|->D!Init(DP),network|->{},txnet|->{},events|-> <<>>,phase|->0,installed|->FALSE,
 sealed|->FALSE,started|->FALSE,copied|->{},oldDead|->FALSE,recovering|->FALSE,recovered|->FALSE,
 cursor|->0,definition|->[dummy|->0],observed|->{},gcPressure|->FALSE,issued|->{},
 controlEvidence|->{},produced|-> <<>>,oldErased|->FALSE,other|->FALSE,
 control|-> <<>>,outbox|->{},uptake|->{},routed|->{},prefixDuplicate|->FALSE,deletedOld|->FALSE,routeKnown|->FALSE,recoveryEvidence|->{},oldSent|->{},uncached|->FALSE,grants|->{},materials|->{}]
(* Prefix import and tail fold commute by buffering actual durable records. A
   duplicate import cannot replace a fold already containing a later outcome. *)
PhaseRank(phase) == CASE phase="absent" -> 0 [] phase="announce" -> 1 [] phase="fix" -> 2 [] OTHER -> 3
(* Resolve is a complete authoritative source fact: immutable final position,
   scope, decision and full effects. It subsumes earlier evidence for this same
   source. Announce's lower bound may differ from the final fixed position.
   This merge is per source; an unrelated source need not await another's tail. *)
TailEvent(m,e) ==
 IF e.kind="root.source" /\ Bug#"arrival-order" /\
    PhaseRank(e.body.phase)<PhaseRank(m.sources[e.dst][e.body.tx].phase)
 THEN m ELSE C!Observe(CP,m,e)
RECURSIVE TailEvents(_,_)
TailEvents(m,es) == IF es= <<>> THEN m ELSE TailEvents(TailEvent(m,Head(es)),Tail(es))
RECURSIVE FoldControl(_,_)
FoldControl(m,cs) == IF cs= <<>> THEN m ELSE
 LET c==Head(cs)
 IN FoldControl(IF c.kind="cut.event" THEN TailEvents(m,c.body.events) ELSE m,Tail(cs))
ControlMaterial(cs) ==
 LET installs==SelectSeq(cs,LAMBDA c:c.kind="cut.install")
 IN IF installs= <<>> THEN EmptyMaterial
 ELSE FoldControl([C!ObserveAll(CP,EmptyMaterial,Head(installs).body.events) EXCEPT !.closures=Head(installs).body.closures],cs)
ReceivedRoots(s,grants,materials) == {r \in DOMAIN s.roots:
 \E g \in grants,m \in materials:
  g.root=r /\ m.root=r /\ g.context=s.roots[r].definition.context /\
  g.c=s.roots[r].definition.cut /\ g.recipe=s.roots[r].definition.recipe.id /\
  m.recipe=g.recipe /\ m.interpretation=g.interpretation}
PhysicalData(s,grants,materials) == R!Data(s,1,UNION
 {s.holds[1][r].copies:r \in {q \in ReceivedRoots(s,grants,materials):s.holds[1][q].phase="held"}})
Rehydrate(m,s,grants,materials) == [m EXCEPT !.data=PhysicalData(s,grants,materials)]
SourceEnvelope(e,s) ==
 IF e.kind="root.source" /\ e.body.phase="resolve"
 THEN LET id==CHOOSE id \in DOMAIN s.commands:s.commands[id].kind="install" /\ s.commands[id].body.tx=e.body.tx
          c==s.commands[id]
      IN [e EXCEPT !.body=@ @@ [context|->c.body.data.outcome.context]]
 ELSE e
TxTake(tr,remaining) ==
 LET raw==CutEvents(tr.emissions)
     es==[i \in 1..Len(raw)|->[slot|->Len(x.events)+i] @@ SourceEnvelope(raw[i],tr.next)]
 IN [x EXCEPT !.tx=Facts(tr.next,tr.emissions),!.txnet=remaining \cup Forward(tr.emissions),
 !.old=IF x.sealed THEN @ ELSE C!ObserveAll(CP,@,es),!.events=@ \o es,!.produced=@ \o es,
 !.outbox=IF x.sealed THEN @ \cup Elements(es) ELSE @]
Early == Crossing \in {"before-import","old-address"}
Active == CASE x.phase=0 -> 1 [] x.phase=1 -> 2 [] x.phase=4 -> 3
 [] x.phase=6 \/ (x.phase=2 /\ x.sealed /\ Early) -> 1 [] OTHER -> 2
TxCandidates == IF x.phase \in {0,1,4,6,7} \/ (x.phase=2 /\ x.sealed /\ Early)
 THEN T!SubmitActions(P,x.tx,Active) \cup T!ReadActions(P,x.tx,Active) \cup
 T!ComputeActions(P,x.tx,Active) \cup T!PublishActions(P,x.tx,Active) \cup
 {T!Grant(P,x.tx,Active,a):a \in {z \in P.shards:T!CanGrant(P,x.tx,Active,z)}} \cup
 {tr \in T!Actions(P,x.tx):tr.tag="tx.source-arrives" /\ (x.phase=6 \/ (x.phase=2 /\ x.sealed /\ Early))} ELSE {}
TxStep == \E tr \in TxCandidates:x'=TxTake(tr,x.txnet)
TxInput == \E e \in x.txnet:
 IF e.kind="journal.submit" THEN \E tr \in T!CommitAndFold(P,x.tx,e.body):x'=TxTake(tr,x.txnet \ {e})
 ELSE IF e.kind="tx.published" THEN x'=[x EXCEPT !.txnet=@ \ {e}]
 ELSE \E tr \in T!Receive(P,x.tx,e):x'=TxTake(tr,x.txnet \ {e})
Advance ==
 \/ /\ x.phase=0 /\ x.tx.fixed[1][1]>0 /\ x'=[x EXCEPT !.phase=1]
 \/ /\ x.phase=1 /\ 1 \in x.tx.registered[2] /\ x'=[x EXCEPT !.phase=2]
 \/ /\ x.phase=4 /\ 3 \in x.tx.published /\ C!Token(1,x.tx.position[3]) \in DOMAIN x.current.data
    /\ x'=[x EXCEPT !.phase=5]
 \/ /\ x.phase=6 /\ 1 \in x.tx.published /\ x.current.sources[1][1].phase="resolve"
    /\ x'=[x EXCEPT !.phase=7]
Start == /\ x.phase=2 /\ ~x.started
 /\ LET b==CHOOSE q \in x.old.requests:q.tx=2
    IN x'=[x EXCEPT !.started=TRUE,!.definition=b,!.network=@ \cup
       {Packet("old",Event("old-acquire","reader","old","root.acquire",Request("old",b)))}]
Seal == /\ x.started /\ ~x.sealed /\ x.roots["old"].roots["old"].phase="live"
 /\ x'=[x EXCEPT !.sealed=TRUE,!.network=@ \cup
 {Packet(Control("old"),Submit(Control("old"),"cut-seal","cut.seal",[events|->x.events,closures|->x.old.closures,target|->Control("new")]))}]
Copy(t) == /\ x.started /\ t \in DOMAIN InitialData \ x.copied
 /\ x.roots["old"].roots["old"].phase="live"
 /\ LET bytes==R!HeldData(x.roots["old"],1,"old")[t]
    IN x'=[x EXCEPT !.copied=@ \cup {t},!.network=@ \cup
      {Packet("new",Event(<<"copy",t>>,"old","new","root.import",[holder|->1,copy|->R!Copy(1,t,2,bytes)]))}]
OldAddress(e) == Crossing="old-address" /\ e.kind="root.source" /\ e.body.tx=1 /\ e.body.phase="resolve"
Route(e) ==
 /\ e \in x.outbox /\ e.id \notin x.routed
 /\ IF OldAddress(e) /\ e.id \notin x.oldSent
     THEN x'=[x EXCEPT !.oldSent=@ \cup {e.id},!.network=@ \cup
       {Packet(Control("old"),Event(<<"old-address",e.id>>,T!Fold(1),Control("old"),"cut.source",e))}]
     ELSE /\ x.routeKnown /\ (~OldAddress(e) \/ x.oldDead)
          /\ x'=[x EXCEPT !.routed=@ \cup {e.id},!.network=IF Bug="omit-tail" /\ e.kind="root.source" /\ e.body.tx=1 /\ e.body.phase="resolve" THEN @ ELSE @ \cup
           {Packet(Control("new"),Submit(Control("new"),<<"tail",e.id>>,"cut.event",[events|-> <<e>>]))}]
(* Source event outbox belongs to the surviving transaction owner, not the lost
   cut controller. The chosen successor route and immutable IDs allow retry of
   an old-address delivery; only the new journal's actual delivery records uptake. *)
RetireOld == /\ x.roots["old"].roots["old"].phase="released"
 /\ x.roots["old"].holds[1]["old"].phase="terminal" /\ ~x.oldDead
 /\ x'=[x EXCEPT !.uncached=(x.tx.material[2][1]=TxNone /\ x.tx.inputs[2][1]=TxNone),!.oldDead=TRUE,!.old=EmptyMaterial,!.oldErased=TRUE,!.current=EmptyMaterial,
    !.installed=FALSE,!.recovering=TRUE,!.cursor=0,!.control= <<>>,!.phase=3,!.network=@ \cup
    {Packet(Control("new"),Event("recover-cut",Control("new"),Control("new"),"journal.recover",[reason|->"owner-reset"]))}]
ControlInput(e) ==
 IF e.kind="journal.deliver"
 THEN LET c==e.body.command
      IN IF e.body.owner=Control("old") /\ c.kind="cut.seal"
         THEN [x EXCEPT !.controlEvidence=@ \cup {c},!.network=@ \cup
          {Packet("producer",Event("successor-route",Control("old"),T!Fold(1),"cut.route",[seal|->c.id,target|->c.body.target])),
           Packet("new",Event("new-acquire","reader","new","root.acquire",Request("new",x.definition))),
           Packet("old",Event("old-close","reader","old","root.close",[root|->"old"])),
           Packet(Control("new"),Submit(Control("new"),"cut-import","cut.install",
             [seal|->c.id,closures|->c.body.closures,events|->IF Bug="omit-pending" THEN SelectSeq(c.body.events,LAMBDA z:z.kind#"root.source") ELSE c.body.events]))}]
         ELSE IF e.body.owner=Control("new") /\ ~x.recovering /\ e.body.index=x.cursor+1
         THEN LET cs==Append(x.control,c)
              IN [x EXCEPT !.control=cs,!.current=Rehydrate(ControlMaterial(cs),x.roots["new"],x.grants,x.materials),!.cursor=e.body.index,
                   !.installed=@ \/ c.kind="cut.install",!.controlEvidence=@ \cup {c},
                   !.uptake=IF c.kind="cut.event" THEN @ \cup {z.id:z \in Elements(c.body.events)} ELSE @,
                   !.network=@]
         ELSE x
 ELSE IF e.kind="journal.snapshot" /\ e.body.owner=Control("new") /\ x.recovering /\ e.body.recovery=ToString("recover-cut")
 THEN [x EXCEPT !.control=e.body.prefix,!.current=Rehydrate(ControlMaterial(e.body.prefix),x.roots["new"],x.grants,x.materials),
      !.recoveryEvidence=@ \cup {e},!.cursor=e.body.index,!.recovering=FALSE,!.recovered=TRUE,!.installed=TRUE,!.phase=4]
 ELSE x
BlockedImport(e) == e.kind="journal.submit" /\ e.body.kind="cut.install" /\
 ((Bug="premature-custody" /\ x.roots["old"].roots["old"].phase#"released") \/
 (Crossing="before-import" /\ ~(\E q \in x.controlEvidence:q.kind="cut.event" /\ Head(q.body.events).body.phase="resolve")))
Input(pkt) ==
 LET e==pkt.event site==pkt.site remaining==x.network \ {pkt}
 IN IF e.kind="cut.source" THEN {}
 ELSE IF e.kind="cut.route" THEN
   IF Crossing="old-address" /\ ~x.oldDead THEN {} ELSE
   {Transition("input",[x EXCEPT !.network=remaining,!.routeKnown=TRUE],<<>>)}
 ELSE IF BlockedImport(e) THEN {}
 ELSE IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input",[x EXCEPT !.journal=tr.next,!.network=remaining \cup
       {Packet(ev.dst,ev):ev \in Elements(tr.emissions)}],<<>>):tr \in D!Receive(DP,x.journal,e)}
 ELSE IF site \in {Control("old"),Control("new")}
 THEN {Transition("input",[ControlInput(e) EXCEPT !.network=(@ \ {pkt})],<<>>)}
 ELSE IF e.kind \in {"Material","ViewGrant"}
 THEN LET grants==IF e.kind="ViewGrant" /\ site="new" THEN x.grants \cup {e.body} ELSE x.grants
          materials==IF e.kind="Material" /\ site="new" THEN x.materials \cup {e.body} ELSE x.materials
      IN {Transition("input",[x EXCEPT !.network=remaining,!.grants=grants,!.materials=materials,
        !.current=IF x.installed THEN Rehydrate(x.current,x.roots["new"],grants,materials) ELSE @],<<>>)}
 ELSE {Transition("input",[x EXCEPT !.roots[site]=tr.next,
      !.network=remaining \cup Packets(site,tr.emissions),
      !.current=IF site="new" /\ x.installed THEN Rehydrate(x.current,tr.next,x.grants,x.materials) ELSE @],<<>>):tr \in R!Receive(RP(site),x.roots[site],e)}
Inputs == UNION {Input(pkt):pkt \in x.network}
JournalSteps == {Transition(tr.tag,[x EXCEPT !.journal=tr.next,
 !.network=@ \cup {Packet(e.dst,e):e \in Elements(tr.emissions)}],<<>>):tr \in D!Actions(DP,x.journal)}
CustodyEvents(es) == [i \in 1..Len(es) |->
 IF es[i].kind="root.custody" THEN [es[i] EXCEPT !.body=@ @@
   [control|->IF x.installed THEN Head(SelectSeq(x.control,LAMBDA c:c.kind="cut.install")) ELSE [dummy|->0],
    futureTailOwner|->T!Fold(1)]] ELSE es[i]]
RSteps == UNION {{Transition(tr.tag,[x EXCEPT !.roots[site]=tr.next,
 !.network=@ \cup Packets(site,CustodyEvents(tr.emissions)),
 !.current=IF site="new" /\ x.installed THEN Rehydrate(x.current,tr.next,x.grants,x.materials) ELSE @],<<>>):
 tr \in {z \in R!Actions(RP(site),x.roots[site]) \cup
   (IF site="old" /\ x.oldDead THEN UNION {R!GC([RP(site) EXCEPT !.gc=TRUE],x.roots[site],1,id):id \in DOMAIN x.roots[site].copies[1]} ELSE {}):
   (site#"old" \/ ~x.oldDead \/ z.tag \in {"backend-start","backend-complete","backend-retire","holder-submit","begin-physical-delete"}) /\
   (z.tag#"send-root-transfer-receipt" \/
      ((x.installed \/ Bug="premature-custody") /\
       (~Early \/ (1 \in x.tx.published /\
          (Crossing#"old-address" \/ (\E e \in x.outbox:e.kind="root.source" /\ e.body.tx=1 /\ e.body.phase="resolve" /\ e.id \in x.oldSent)))))) /\
   (z.tag#"serve-retained-recipe" \/ (site="new" /\ x.recovered))}}:site \in Sites}
Rank(tag) == CASE tag="input" -> 0 [] tag="log.deliver" -> 1 [] tag="log.choose" -> 2
 [] tag="root-propose" -> 3 [] tag="send-root-hold" -> 4 [] tag="holder-submit" -> 5
 [] tag="backend-start" -> 6 [] tag="backend-complete" -> 7 [] tag="backend-retire" -> 8
 [] tag="holder-durable-receipt" -> 9 [] OTHER -> 10
ServiceMoves == {tr \in Inputs \cup JournalSteps \cup RSteps:tr.next#x}
Service ==
 /\ ServiceMoves#{}
 /\ LET tr==CHOOSE z \in ServiceMoves:\A q \in ServiceMoves:Rank(z.tag)<=Rank(q.tag)
     IN x'=tr.next
DuplicatePrefix ==
 /\ x.recovered /\ ~x.prefixDuplicate /\ x.current.sources[1][1].phase="resolve"
 /\ LET c==Head(SelectSeq(x.control,LAMBDA z:z.kind="cut.install"))
    IN x'=[x EXCEPT !.prefixDuplicate=TRUE,!.network=@ \cup
      {Packet(Control("new"),Event("duplicate-prefix",Control("old"),Control("new"),"journal.submit",c))}]
LocalSourceEvents == UNION {Elements(c.body.events):c \in {z \in Elements(x.control):z.kind \in {"cut.install","cut.event"}}}
PersistResult(v) == /\ x.recovered /\ v.tx \in {1,3} /\ v \in x.current.pending /\ v.token \notin x.issued
 /\ LET root==IF v.tx=1 THEN "future" ELSE "head"
        b==[Request("new",x.definition) EXCEPT !.root=root,!.recipe=C!Recipe(v.key,v.cut),!.cut=v.cut,!.context=(CHOOSE e \in LocalSourceEvents:e.kind="root.source" /\ e.body.tx=v.tx /\ e.body.phase="resolve").body.context,!.successor="",!.predecessor="",!.view=root]
    IN x'=[x EXCEPT !.issued=@ \cup {v.token},!.network=@ \cup
       {Packet("new",Event(<<"result",v.token>>,Control("new"),"new","root.import",
          [holder|->1,copy|->R!Copy(1,v.token,2,C!Datum(v.value))])),
        Packet("new",Event(<<"result-root",v.token>>,Control("new"),"new","root.acquire",b))}]
MaterialMoves == IF x.recovered /\ x.installed /\ (x.gcPressure \/ Bug="omit-pending") THEN
 UNION {C!Protection(CP,x.current,b) \cup C!Ready(CP,x.current,b) \cup C!Unavailable(CP,x.current,b):b \in x.current.requests} ELSE {}
MaterialStep == \E tr \in MaterialMoves:
 x'=[x EXCEPT !.current=tr.next,!.txnet=@ \cup Events(tr.emissions),
    !.observed=@ \cup {e.body:e \in {z \in Elements(tr.emissions):z.kind="root.recipe-ready"}}]
Pressure == /\ x.phase=5 /\ ~x.gcPressure /\ Fallback \notin C!Heads(CP,x.current)
 /\ \A id \in {z \in DOMAIN x.roots["new"].copies[1]:x.roots["new"].copies[1][z].token=Fallback}:
    R!GC([RP("new") EXCEPT !.gc=TRUE],x.roots["new"],1,id)={}
 /\ x'=[x EXCEPT !.gcPressure=TRUE,!.phase=6]
Done == x.recovered /\ x.prefixDuplicate /\ DOMAIN x.roots["old"].copies[1]={} /\ x.gcPressure /\ {1,2,3} \subseteq x.tx.published /\ x.observed#{}
Other == Bug="omit-tail" /\ x.phase>=4 /\ x'=[x EXCEPT !.other=~@]
Next == IF ENABLED Advance THEN Advance ELSE IF ENABLED Start THEN Start ELSE IF ENABLED Seal THEN Seal
 ELSE IF ENABLED RetireOld THEN RetireOld ELSE IF ServiceMoves#{} THEN Service
 ELSE IF ENABLED DuplicatePrefix THEN DuplicatePrefix
 ELSE IF ENABLED Pressure THEN Pressure ELSE IF ENABLED TxInput THEN TxInput
 ELSE (\E t \in DOMAIN InitialData:Copy(t)) \/ (\E v \in x.current.pending:PersistResult(v)) \/
 (\E e \in x.outbox:Route(e)) \/ MaterialStep \/ TxStep \/ Other \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next) /\ WF_vars(TxStep) /\ WF_vars(MaterialStep) /\
 WF_vars(\E v \in x.current.pending:PersistResult(v)) /\ WF_vars(\E e \in x.outbox:Route(e)) /\ WF_vars(\E t \in DOMAIN InitialData:Copy(t))
NoPrematureRead == x.observed#{} => x.current.sources[1][1].phase="resolve"
ExactRead == \A b \in x.observed:b.values[1]=(IF Result="value" THEN 7 ELSE 0) /\ b.cut=x.definition.cut /\ b.context=x.definition.context
HeldExists == \A s \in Sites:R!HeldExists(RP(s),x.roots[s])
LiveRetained == \A s \in Sites:R!LiveRetained(RP(s),x.roots[s])
CustodyControl == \A b \in x.roots["old"].custody:
 b.control \in Elements(x.journal.log[Control("new")]) /\ b.control.kind="cut.install" /\
 b.futureTailOwner=T!Fold(1) /\
 b.control.body.closures[ToString(T!Root(2,1))].pending={ToString(<<1,1>>)}
CustodyClosure == x.oldDead => x.roots["new"].roots["new"].phase="live"
CapturedPending == x.started => x.definition.cut>x.tx.position[1]
CutProvenance == x.started => \E id \in DOMAIN x.tx.commands:
 LET c==x.tx.commands[id] IN c.kind="bound" /\ c.body.tx=2 /\ c.body.key=1 /\
 c.body.data.cut=x.definition.cut /\ c.body.data.context=x.definition.context
ImportProvenance == \A c \in {q \in x.controlEvidence:q.kind="cut.install"}:
 \E seal \in Elements(x.journal.log[Control("old")]):seal.id=c.body.seal /\ seal.kind="cut.seal" /\ seal.body.events=c.body.events
PendingResponsibility == x.roots["old"].roots["old"].phase="released" =>
 /\ \E id \in DOMAIN x.tx.commands:
      LET c==x.tx.commands[id] IN c.owner=1 /\ c.kind="fix" /\ c.body.tx=1 /\ c.body.data=x.tx.position[1]
 /\ (x.tx.ticket[1][1]="resolved" => \E e \in x.outbox:
       e.kind="root.source" /\ e.body.tx=1 /\ e.body.phase="resolve")
SnapshotSound == \A e \in x.recoveryEvidence:D!DeliverySound(x.journal.log,e)
ResolutionProvenance == \A c \in {q \in x.controlEvidence:q.kind="cut.event"}:\A e \in Elements(c.body.events):e \in Elements(x.produced)
CompletedShape == Done => x.uncached /\ x.oldErased /\ x.tx.position[1]<x.definition.cut /\ x.definition.cut<x.tx.position[3] /\
 x.tx.outcome[2].effects[1]=(IF Result="value" THEN 8 ELSE 1)
SourceTerminal == x.installed => \A e \in {z \in LocalSourceEvents:z.kind="root.source" /\ z.body.phase="resolve"}:
 /\ x.current.sources[e.dst][e.body.tx].phase="resolve"
 /\ x.current.sources[e.dst][e.body.tx].cut=e.body.cut
 /\ x.current.sources[e.dst][e.body.tx].effects=e.body.effects
 /\ x.current.sources[e.dst][e.body.tx].scopes=e.body.scopes
 /\ x.current.sources[e.dst][e.body.tx].decision=ToString(e.body.decision)
OutboxDerivable == \A e \in {z \in x.outbox:z.kind="root.source" /\ z.body.phase="resolve"}:
 \E id \in DOMAIN x.tx.commands:
  LET c==x.tx.commands[id] IN c.kind="install" /\ c.body.tx=e.body.tx /\
  c.body.data.position=e.body.cut /\ c.body.data.decision=e.body.decision /\
  c.body.data.outcome.effects=e.body.effects /\ c.body.data.outcome.context=e.body.context
ReverseTail == \E i,j \in 1..Len(x.control):i<j /\
 x.control[i].kind="cut.event" /\ x.control[j].kind="cut.event" /\
 LET a==Head(x.control[i].body.events) b==Head(x.control[j].body.events)
 IN a.kind="root.source" /\ b.kind="root.source" /\ a.body.tx=b.body.tx /\
 PhaseRank(a.body.phase)>PhaseRank(b.body.phase)
NoReverseTail == ~(Done /\ ReverseTail)
MaterialAuthenticity == \A b \in x.materials:
 b.bytes=M!Reconstruct(R!HeldData(x.roots["new"],1,b.root),x.roots["new"].roots[b.root].definition.recipe)
RetainedLateResult == (x.observed#{} /\ Result="value") =>
 x.roots["new"].holds[1]["future"].phase="held" /\
 C!Token(1,x.tx.position[1]) \notin C!Heads(CP,x.current)
Completes == <>Done
NoCompletedTransfer == ~Done
Trace == [phase|->x.phase,sourcePosition|->x.tx.position,source|->x.current.sources,
 cut|->x.definition,closure|->x.current.closures,roots|->[s \in Sites|->x.roots[s].roots],
 pending|->x.network,oldDead|->x.oldDead,recovered|->x.recovered,observed|->x.observed,
 published|->x.tx.published,pressure|->x.gcPressure]
=============================================================================
