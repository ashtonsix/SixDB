--------------------------- MODULE RecoveryKernel ---------------------------
EXTENDS Contracts, Integers
M == INSTANCE MaterialCore
V == INSTANCE ViewsKernel

\* Owner decisions are replicated commands; holder ledgers use ordered durable
\* operations. Immutable physical-copy IDs prevent old deletes erasing refetches.
EmptyRoot == [phase |-> "vacant", definition |-> [dummy |-> 0]]
EmptyHold == [phase |-> "empty", copies |-> {}]
CopyId(h,t,g) == ToString(<<h,t,g>>)
Copy(h,t,g,data) == [id |-> CopyId(h,t,g), token |-> t, generation |-> g,
                    data |-> data]
Init(p) ==
 [roots |-> [r \in p.roots |-> EmptyRoot], requested |-> {}, definitions |-> [r \in p.roots |-> [dummy |-> 0]],
  holds |-> [h \in p.holders |-> [r \in p.roots |-> EmptyHold]],
  localHolds |-> [h \in p.holders |-> [r \in p.roots |-> EmptyHold]],
  ready |-> [h \in p.holders |-> TRUE],
  copies |-> [h \in p.holders |->
    [id \in {CopyId(h,t,1):t \in p.initial[h]} |->
      LET t == CHOOSE x \in p.initial[h]:CopyId(h,x,1)=id
      IN Copy(h,t,1,p.data[t])]],
  deleting |-> [h \in p.holders |-> {}],
  registry |-> [h \in p.holders |-> V!RegistryInit],
  metadata |-> [h \in p.holders |-> ""],
  holdRequests |-> [h \in p.holders |-> {}],
  terminalRequests |-> [h \in p.holders |-> {}],
  holderSent |-> {},
  receipts |-> {}, refusals |-> {}, submitted |-> {}, sent |-> {}, seen |-> {},
  cursor |-> 0, up |-> TRUE, resets |-> 0, holderResets |-> {},
  granted |-> {}, closed |-> {}, observations |-> {},
  observationDefinitions |-> [r \in p.roots |-> [dummy |-> 0]],
  imports |-> {}, imported |-> {}, custody |-> {}, custodyRequests |-> {},
  fetched |-> {}, fetchRequests |-> [h \in p.holders |-> {}], copied |-> {},
  protected |-> {}, terminalHistory |-> {},
  holderTerminal |-> [h \in p.holders |-> {}],lateWrites |-> FALSE]

Emit(p,id,src,dst,kind,body) == Event(id,src,dst,kind,body)
RecoveryEvent(p,n) == Emit(p,<<"recover",p.actor,n>>,p.actor,p.owner,"journal.recover",[recovery|->n])

Submit(p,s,r,kind,body) ==
 LET c == Command(p.owner,<<kind,r>>,kind,body)
 IN Transition("root-propose",[s EXCEPT !.submitted=@ \cup {c.id}],
      <<Emit(p,<<"submit",c.id>>,p.actor,p.owner,"journal.submit",c)>>)
Definition(p,s,r) == s.definitions[r]
\* Owner restart does not reset another process's reply suppression. These
\* caches have no durable inbox: only chosen records rebuild logical authority.
ColdOwner(p,s) ==
 [s EXCEPT !.roots=[r \in p.roots |-> EmptyRoot],!.requested={},
   !.definitions=[r \in p.roots |-> [dummy |-> 0]],!.receipts={},!.refusals={},
   !.custody={},!.custodyRequests={},!.fetched={},!.closed={},!.submitted={},!.sent={},!.seen={},!.cursor=0]
CurrentReply(s,b) ==
 b.recovery=s.resets /\ b.query \in s.sent

Apply(p,s,c) ==
 IF c.id \in s.seen THEN s
 ELSE LET r == c.body.root
          n == [s EXCEPT !.seen=@ \cup {c.id}]
      IN CASE c.kind="root.begin" ->
            IF s.roots[r].phase="vacant"
            THEN [n EXCEPT !.roots[r]=[phase |-> "begun",definition |-> c.body],
                           !.definitions[r]=c.body,!.requested=@ \cup {r}] ELSE n
         [] c.kind="root.register" ->
            IF s.roots[r].phase="begun"
            THEN [n EXCEPT !.roots[r].phase="live",!.protected=@ \cup {r}] ELSE n
         [] c.kind="root.abort" ->
            IF s.roots[r].phase \in {"vacant","begun"}
            THEN [n EXCEPT !.roots[r]=[phase |-> "aborted",definition |-> c.body],
                          !.terminalHistory=@ \cup {r}] ELSE n
         [] c.kind="root.release" ->
            IF s.roots[r].phase="live"
            THEN [n EXCEPT !.roots[r].phase="released",!.terminalHistory=@ \cup {r}] ELSE n
         [] OTHER -> n

RECURSIVE Replay(_,_,_)
Replay(p,s,cs) == IF cs= <<>> THEN s
 ELSE Replay(p,IF Head(cs).kind \in {"root.begin","root.register","root.abort","root.release"}
               THEN Apply(p,s,Head(cs)) ELSE s,Tail(cs))

Available(p,s,h,recipe) ==
 {id \in DOMAIN s.copies[h]:s.copies[h][id].token \in M!Needed(recipe) /\ id \notin s.deleting[h]}
Tokens(s,h,ids) == {s.copies[h][id].token:id \in ids}
Data(s,h,ids) ==
 [t \in Tokens(s,h,ids) |-> (CHOOSE cp \in {s.copies[h][id]:id \in ids}:cp.token=t).data]
HeldData(s,h,r) == Data(s,h,s.holds[h][r].copies)


\* A repeated RPC may replay a response; idempotence suppresses effects, not
\* replies. These pure constructors inspect only the receiver's loaded durable
\* authority/material. First sends and repeated requests share the same guards.
HoldReply(p,s,h,b) ==
 IF s.ready[h] /\ s.localHolds[h][b.root].phase="held"
 THEN <<Emit(p,<<"hold-receipt",h,b.query>>,h,p.actor,"root.receipt",
   [root|->b.root,holder|->h,copies|->s.localHolds[h][b.root].copies,
    recovery|->b.recovery,query|->b.query])>> ELSE <<>>
RefusalReply(p,s,h,b) ==
 IF s.ready[h] /\ s.metadata[h]="" /\ s.localHolds[h][b.root].phase="empty" /\
    ~(M!Needed(b.recipe) \subseteq Tokens(s,h,Available(p,s,h,b.recipe))) /\
    (IF "waitForMaterial" \in DOMAIN b THEN ~b.waitForMaterial ELSE TRUE)
 THEN <<Emit(p,<<"refuse",h,b.query>>,h,p.actor,"root.refused",
   [root|->b.root,holder|->h,recovery|->b.recovery,query|->b.query])>> ELSE <<>>
FetchReply(p,s,b,h) ==
 IF s.ready[h] /\ s.localHolds[h][b.root].phase="held"
 THEN LET data==Data(s,h,s.localHolds[h][b.root].copies)
      IN IF M!WellTyped(data,b.recipe)
         THEN <<Emit(p,<<"bytes",b.root,h,b.query>>,h,p.actor,"root.bytes",
          [root|->b.root,recipe|->b.recipe.id,interpretation|->b.recipe.interpretation,
           copy|->ToString(s.holds[h][b.root].copies),storageIncarnation|->1,
           bytes|->M!Reconstruct(data,b.recipe),holder|->h,recovery|->b.recovery,query|->b.query])>>
         ELSE <<>> ELSE <<>>
CustodyReply(p,s,q) ==
 LET r==q.successor
 IN IF s.up /\ s.roots[r].phase \in {"live","released"}
    THEN LET b==Definition(p,s,r)
         IN IF "predecessor" \in DOMAIN b /\ ToString(b.predecessor)=ToString(q.root) /\
               ToString(b.predecessorOwner)=ToString(q.requester) /\ b.cut=q.cut /\ b.context=q.context
            THEN <<Emit(p,<<"custody",q.requester,q.query>>,p.actor,q.requester,"root.custody",
              [root|->q.root,successor|->r,cut|->b.cut,context|->b.context,generation|->q.generation,
               recovery|->q.recovery,query|->q.query])>> ELSE <<>> ELSE <<>>
ReplyIDs(es) == {e.id:e \in Elements(es)}

Receive(p,s,e) ==
 CASE e.kind="journal.deliver" ->
       IF s.up /\ e.body.owner=p.owner /\ e.body.index=s.cursor+1
       THEN {Transition("root-journal-delivery",
          [IF e.body.command.kind \in {"root.begin","root.register","root.abort","root.release"}
           THEN Apply(p,s,e.body.command) ELSE s EXCEPT !.cursor=e.body.index],<<>>)} ELSE IF e.body.index <= s.cursor THEN {Transition("old-root-delivery",s,<<>>)} ELSE {}
   [] e.kind="journal.snapshot" ->
       IF ~s.up /\ e.body.owner=p.owner /\ e.body.recovery=ToString(<<"recover",p.actor,s.resets>>)
       THEN LET base == [s EXCEPT !.roots=[r \in p.roots |-> EmptyRoot],!.seen={}]
            IN {Transition("root-owner-recovered",
                  [Replay(p,base,e.body.prefix) EXCEPT !.up=TRUE,!.cursor=e.body.index,!.submitted={},!.sent={} ],<<>>)} ELSE {}
   [] e.kind="root.acquire" ->
       IF s.up /\ e.body.root \in p.roots
       THEN {Transition("root-acquire-request",[s EXCEPT !.requested=@ \cup {e.body.root},
         !.definitions[e.body.root]=IF e.body.root \in s.requested THEN @ ELSE e.body,
         \* A repeated acquisition asks for the same durable outcome again.
         \* Rearm response delivery only, never the acquisition or its holds.
         !.sent=IF p.bad="suppress-repeat-acquire" THEN @
                ELSE @ \ {ToString(<<kind,e.body.root>>):kind \in {"grant","material","failure"}}],<<>>)} ELSE {}
   [] e.kind="root.hold" ->
       {Transition("root-hold-request",[s EXCEPT !.holdRequests[e.dst]=@ \cup {e.body}],<<>>)}
   [] e.kind="root.terminal" ->
       {Transition("root-terminal-request",[s EXCEPT !.terminalRequests[e.dst]=@ \cup {e.body.root}],<<>>)}
   [] e.kind="root.refused" ->
       IF s.up THEN {Transition("root-holder-refusal",
         IF CurrentReply(s,e.body) THEN [s EXCEPT !.refusals=@ \cup {e.body}] ELSE s,<<>>)} ELSE {}
   [] e.kind="root.receipt" ->
       IF s.up THEN {Transition("root-receipt",
         IF CurrentReply(s,e.body) THEN [s EXCEPT !.receipts=@ \cup {e.body}] ELSE s,<<>>)} ELSE {}
   [] e.kind="root.import" ->
       {Transition("receive-immutable-transfer",[s EXCEPT !.imports=@ \cup {e.body}],<<>>)}
   [] e.kind="root.custody-query" ->
       IF s.up /\ e.body.successor \in p.roots
       THEN {Transition("receive-root-transfer-query",[s EXCEPT !.custodyRequests=@ \cup {e.body}],<<>>)} ELSE {}
   [] e.kind="root.custody" ->
       IF s.up THEN {Transition("receive-root-transfer-receipt",
         IF CurrentReply(s,e.body) THEN [s EXCEPT !.custody=@ \cup {e.body}] ELSE s,<<>>)} ELSE {}
   [] e.kind="root.fetch" ->
       {Transition("root-fetch-request",[s EXCEPT !.fetchRequests[e.dst]=@ \cup {e.body}],<<>>)}
   [] e.kind="root.bytes" ->
       IF s.up THEN {Transition("root-fetched-bytes",
         IF CurrentReply(s,e.body) THEN [s EXCEPT !.fetched=@ \cup {e.body}] ELSE s,<<>>)} ELSE {}
   [] e.kind="root.close" ->
       IF s.up THEN {Transition("root-client-close",[s EXCEPT !.closed=@ \cup {e.body.root}],<<>>)} ELSE {}
   [] OTHER -> {}

Begin(p,s,r) ==
 IF s.up /\ r \in s.requested /\ s.roots[r].phase="vacant" /\
    ToString(<<"root.begin",r>>) \notin s.submitted
 THEN {Submit(p,s,r,"root.begin",Definition(p,s,r))} ELSE {}
SendHold(p,s,r,h,n) ==
 LET id == <<"hold",r,h,s.resets,n>>
 IN IF s.up /\ s.roots[r].phase="begun" /\ h \in s.roots[r].definition.holders /\
       ToString(id) \notin s.sent
    THEN {Transition("send-root-hold",[s EXCEPT !.sent=@ \cup {ToString(id)}],
            <<Emit(p,id,p.actor,h,"root.hold",s.roots[r].definition @@
               [recovery |-> s.resets,query |-> ToString(id)])>>)} ELSE {}

RegisterOp(p,s,h,id,kind,payload) ==
 LET req == [id |-> ToString(id),generation |-> s.registry[h].generation,
             target |-> h,kind |-> kind,payload |-> payload]
 IN {Transition("holder-submit",[s EXCEPT !.registry[h]=tr.next,
                !.metadata[h]=req.id],<<>>):tr \in V!RegistryRegister(s.registry[h],req)}
Hold(p,s,h,b) ==
 LET r == b.root
     ids == Available(p,s,h,b.recipe)
 IN IF s.ready[h] /\ s.metadata[h]="" /\ (s.localHolds[h][r].phase="empty" \/ p.bad="resurrect-hold") /\
       M!Needed(b.recipe) \subseteq Tokens(s,h,ids)
    THEN RegisterOp(p,s,h,<<"hold",r>>, "hold",[root |-> r,copies |-> ids]) ELSE {}
RefuseHold(p,s,h,b) ==
 LET es==RefusalReply(p,s,h,b)
 IN IF b \in s.holdRequests[h] /\ es# <<>> /\
       (p.bad#"suppress-repeat-reply" \/ Head(es).id \notin s.holderSent)
    THEN {Transition("refuse-missing-recipe",[s EXCEPT !.holderSent=@ \cup ReplyIDs(es),
           !.holdRequests[h]=@ \ {b}],es)} ELSE {}

TerminalHold(p,s,h,r) ==
 IF s.ready[h] /\ s.metadata[h]="" /\ s.localHolds[h][r].phase#"terminal"
 THEN RegisterOp(p,s,h,<<"terminal",r>>, "terminal",[root |-> r,copies |-> {}]) ELSE {}

\* No operation overtakes an older metadata operation on this holder. Its disk
\* effect occurs at backend completion, even after the owner process has died.
Physical(p,s,h,request) ==
 LET b == request.payload
 IN CASE request.kind="hold" ->
       IF s.holds[h][b.root].phase="terminal" /\ p.bad#"resurrect-hold" THEN s
       ELSE [s EXCEPT !.holds[h][b.root]=[phase |-> "held",copies |-> b.copies]]
    [] request.kind="terminal" ->
       [s EXCEPT !.holds[h][b.root]=[phase |-> "terminal",copies |-> {}],
                 !.holderTerminal[h]=@ \cup {b.root}]
    [] request.kind="delete" ->
       [s EXCEPT !.copies[h]=[id \in DOMAIN @ \ {b.copy} |-> @[id]]]
    [] request.kind="copy" ->
       [s EXCEPT !.copies[h]=@ @@ (b.copy.id :> b.copy),
                 !.lateWrites=@ \/ h \in s.holderResets]
    [] OTHER -> s
RECURSIVE PhysicalEvents(_,_,_,_)
PhysicalEvents(p,s,h,es) ==
 IF es= <<>> THEN s ELSE
 PhysicalEvents(p,IF Head(es).kind="BackendCompleted" THEN Physical(p,s,h,Head(es).body)
                 ELSE IF Head(es).kind="BackendRetired" /\ Head(es).body.id=s.metadata[h]
                      THEN [s EXCEPT !.metadata[h]="",
                              !.localHolds[h]=IF s.ready[h] THEN s.holds[h] ELSE @]
                      ELSE s,h,Tail(es))
Backend(p,s,h) ==
 {Transition(tr.tag,PhysicalEvents(p,[s EXCEPT !.registry[h]=tr.next],h,tr.emissions),<<>>):
   tr \in V!RegistryActions([actor |-> h,owner |-> p.actor],s.registry[h])}
HolderRestart(p,s,h) ==
 IF p.reset /\ h \notin s.holderResets
 THEN {Transition("holder-process-reset",[s EXCEPT !.holderResets=@ \cup {h},
        !.registry[h]=tr.next,!.ready[h]=FALSE,
        !.localHolds[h]=[r \in p.roots |-> EmptyHold]],<<>>):tr \in V!RegistryClose(s.registry[h])} ELSE {}
HolderReopen(p,s,h) ==
 {Transition("holder-recovered",[s EXCEPT !.registry[h]=tr.next,
               !.ready[h]=TRUE,!.localHolds[h]=s.holds[h]],<<>>):
   tr \in V!RegistryReopen(s.registry[h])}

\* An already-held root answers a new query without a new metadata write.
\* The holder retains old reply suppression across an unrelated owner reset.
Receipt(p,s,h,b) ==
 LET es==HoldReply(p,s,h,b)
 IN IF b \in s.holdRequests[h] /\ es# <<>> /\
       (p.bad#"suppress-repeat-reply" \/ Head(es).id \notin s.holderSent)
    THEN {Transition("holder-durable-receipt",[s EXCEPT !.holderSent=@ \cup ReplyIDs(es),
           !.holdRequests[h]=@ \ {b}],es)} ELSE {}

Register(p,s,r) ==
 IF s.up /\ s.roots[r].phase="begun" /\
    (s.roots[r].definition.holders \subseteq {b.holder:b \in {x \in s.receipts:x.root=r}} \/ p.bad="unheld-register") /\
    ToString(<<"root.register",r>>) \notin s.submitted
 THEN {Submit(p,s,r,"root.register",s.roots[r].definition)} ELSE {}
Abort(p,s,r) ==
 IF (p.abort \/ \E b \in s.refusals:b.root=r) /\
    s.up /\ s.roots[r].phase="begun" /\
    ToString(<<"root.abort",r>>) \notin s.submitted
 THEN {Submit(p,s,r,"root.abort",s.roots[r].definition @@
               [reason |-> IF p.abort THEN "cancelled" ELSE "unavailable"])} ELSE {}
Release(p,s,r) ==
 IF s.up /\ s.roots[r].phase="live" /\ r \in s.closed /\
    (ToString(Definition(p,s,r).successor)=ToString("") \/
       (\E b \in s.custody:b.root=r /\ b.successor=Definition(p,s,r).successor /\
          b.cut=Definition(p,s,r).cut /\ b.context=Definition(p,s,r).context /\
          b.generation=Definition(p,s,r).generation) \/
       p.bad="early-transfer") /\
    ToString(<<"root.release",r>>) \notin s.submitted
 THEN {Submit(p,s,r,"root.release",s.roots[r].definition)} ELSE {}
SendTerminal(p,s,r,h) ==
 LET id == <<"terminal",r,h>>
 IN IF s.up /\ s.roots[r].phase \in {"aborted","released"} /\ ToString(id) \notin s.sent
    THEN {Transition("release-holder-reservation",[s EXCEPT !.sent=@ \cup {ToString(id)}],
          <<Emit(p,id,p.actor,h,"root.terminal",[root |-> r])>>)} ELSE {}
Grant(p,s,r) ==
 IF s.up /\ s.roots[r].phase="live" /\ ToString(<<"grant",r>>) \notin s.sent
 THEN LET b == s.roots[r].definition
      IN {Transition("root-view-grant",[s EXCEPT !.granted=@ \cup {r},
                 !.sent=@ \cup {ToString(<<"grant",r>>)}],
        <<Emit(p,<<"grant",r>>,p.actor,b.requester,"ViewGrant",
            [view |-> b.view,context |-> b.context,c |-> b.cut,root |-> r,
             generation |-> b.generation,recipe |-> b.recipe.id,
             interpretation |-> b.recipe.interpretation,rights |-> b.rights])>>)} ELSE {}
Failure(p,s,r) ==
 LET id==<<"failure",r>>
 IN IF s.up /\ s.roots[r].phase="aborted" /\ ToString(id) \notin s.sent
    THEN LET b==s.roots[r].definition
         IN {Transition("root-failure-result",[s EXCEPT !.sent=@ \cup {ToString(id)}],
           <<Emit(p,id,p.actor,b.requester,"RootFailure",
             [root|->r,cut|->b.cut,context|->b.context,generation|->b.generation,reason|->b.reason])>>)} ELSE {}

\* Live authority is already in the journal. A read can ask every declared
\* holder independently; it does not reacquire all registration receipts.
SendFetch(p,s,r,h,n) ==
 LET id == <<"fetch",r,h,s.resets,n>>
 IN IF s.up /\ s.roots[r].phase="live"
    THEN IF h \in s.roots[r].definition.holders /\ ToString(id) \notin s.sent
         THEN {Transition("send-root-fetch",[s EXCEPT !.sent=@ \cup {ToString(id)}],
           <<Emit(p,id,p.actor,h,"root.fetch",s.roots[r].definition @@
             [recovery |-> s.resets,query |-> ToString(id)])>>)} ELSE {}
    ELSE {}
Fetch(p,s,b,h) ==
 LET es==FetchReply(p,s,b,h)
 IN IF b \in s.fetchRequests[h] /\ es# <<>> /\
       (p.bad#"suppress-repeat-reply" \/ Head(es).id \notin s.holderSent)
    THEN {Transition("serve-retained-recipe",[s EXCEPT !.holderSent=@ \cup ReplyIDs(es),
           !.fetchRequests[h]=@ \ {b}],es)} ELSE {}

DeliverMaterial(p,s,b) ==
 LET id == <<"material",b.root>>
 IN IF s.up /\ s.roots[b.root].phase="live" /\ ToString(id) \notin s.sent
    THEN {Transition("deliver-retained-material",[s EXCEPT !.sent=@ \cup {ToString(id)},
            !.observations=@ \cup {b},!.observationDefinitions[b.root]=Definition(p,s,b.root)],
          <<Emit(p,id,p.actor,Definition(p,s,b.root).requester,"Material",b)>>)} ELSE {}

GC(p,s,h,id) ==
 IF p.gc /\ s.ready[h] /\ s.metadata[h]="" /\ id \notin s.deleting[h] /\
    (~(\E r \in p.roots:s.localHolds[h][r].phase="held" /\ id \in s.localHolds[h][r].copies) \/ p.bad="collect-held")
 THEN {Transition("begin-physical-delete",[tr.next EXCEPT !.deleting[h]=@ \cup {id}],<<>>):
       tr \in RegisterOp(p,s,h,<<"delete",id>>,"delete",[copy |-> id])} ELSE {}
CopyTo(p,s,src,dst,id) ==
 LET cp == s.copies[src][id]
     key == <<src,dst,cp.token>>
     fresh == Copy(dst,cp.token,2,cp.data)
 IN IF src#dst /\ key \notin s.copied /\ s.metadata[dst]=""
    THEN {Transition("copy-immutable-material",[tr.next EXCEPT !.copied=@ \cup {key}],<<>>):
       tr \in RegisterOp(p,s,dst,<<"copy",key>>,"copy",[copy |-> fresh])} ELSE {}
Import(p,s,b) ==
 IF b.copy.id \notin s.imported /\ s.metadata[b.holder]=""
 THEN {Transition("persist-transferred-material",[tr.next EXCEPT !.imported=@ \cup {b.copy.id}],<<>>):
       tr \in RegisterOp(p,s,b.holder,<<"import",b.copy.id>>,"copy",[copy |-> b.copy])} ELSE {}
\* Custody uses the same ordinary request/correlation rule as hold/fetch.
\* Its query route survives in the source definition; a missing reply never
\* authorizes retirement. A replayed adopted successor can answer again.
SendCustodyQuery(p,s,r,n) ==
 IF s.up /\ s.roots[r].phase="live"
 THEN LET b==Definition(p,s,r)
          id==<<"custody-query",r,s.resets,n>>
          target==IF "successorOwner" \in DOMAIN b THEN b.successorOwner ELSE p.actor
      IN IF ToString(b.successor)#ToString("") /\ ToString(id) \notin s.sent
         THEN {Transition("send-root-transfer-query",[s EXCEPT !.sent=@ \cup {ToString(id)}],
          <<Emit(p,id,p.actor,target,"root.custody-query",
            [root|->r,successor|->b.successor,cut|->b.cut,context|->b.context,
             generation|->b.generation,requester|->p.actor,recovery|->s.resets,query|->ToString(id)])>>)} ELSE {}
 ELSE {}
Custody(p,s,q) ==
 LET es==CustodyReply(p,s,q)
 IN IF q \in s.custodyRequests /\ es# <<>> /\
       (p.bad#"suppress-repeat-reply" \/ Head(es).id \notin s.sent)
    THEN {Transition("send-root-transfer-receipt",[s EXCEPT !.sent=@ \cup ReplyIDs(es),
           !.custodyRequests=@ \ {q}],es)} ELSE {}

OwnerReset(p,s) ==
 IF p.reset /\ s.up /\ s.resets=0
 THEN {Transition("root-owner-reset",[ColdOwner(p,s) EXCEPT !.up=FALSE,!.resets=1],
       <<RecoveryEvent(p,1)>>)} ELSE {}

Actions(p,s) ==
 OwnerReset(p,s) \cup
 UNION {Import(p,s,b):b \in s.imports} \cup
 UNION {Custody(p,s,q):q \in s.custodyRequests} \cup
 UNION {SendCustodyQuery(p,s,r,n):r \in p.roots,n \in 1..p.retries} \cup
 UNION {Begin(p,s,r) \cup Register(p,s,r) \cup Abort(p,s,r) \cup Release(p,s,r) \cup Grant(p,s,r) \cup Failure(p,s,r):r \in p.roots}
 \cup UNION {SendHold(p,s,r,h,n) \cup SendFetch(p,s,r,h,n):r \in p.roots,h \in p.holders,n \in 1..p.retries}
 \cup UNION {SendTerminal(p,s,r,h):r \in p.roots,h \in p.holders}
 \cup UNION {Backend(p,s,h) \cup HolderRestart(p,s,h) \cup HolderReopen(p,s,h):h \in p.holders}
 \cup UNION {UNION {Hold(p,s,h,b) \cup RefuseHold(p,s,h,b) \cup Receipt(p,s,h,b):b \in s.holdRequests[h]}:h \in p.holders}
 \cup UNION {UNION {TerminalHold(p,s,h,r):r \in s.terminalRequests[h]}:h \in p.holders}
 \cup UNION {UNION {GC(p,s,h,id):id \in DOMAIN s.copies[h]}:h \in p.holders}
 \cup UNION {UNION {CopyTo(p,s,h,d,id):id \in DOMAIN s.copies[h]}:h \in p.holders,d \in p.holders}
 \cup UNION {UNION {Fetch(p,s,b,h):b \in s.fetchRequests[h]}:h \in p.holders}
 \cup UNION {DeliverMaterial(p,s,b):b \in s.fetched}

HeldExists(p,s) == \A h \in p.holders: \A r \in p.roots:
 s.holds[h][r].phase="held" => s.holds[h][r].copies \subseteq DOMAIN s.copies[h]
LiveRetained(p,s) == \A r \in p.roots:s.roots[r].phase="live" =>
 \A h \in s.roots[r].definition.holders:
  /\ s.holds[h][r].phase="held"
  /\ M!WellTyped(HeldData(s,h,r),s.roots[r].definition.recipe)
NoResurrection(p,s) ==
 /\ s.up => \A r \in s.terminalHistory:s.roots[r].phase \in {"released","aborted"}
 /\ \A h \in p.holders: \A r \in s.holderTerminal[h]:s.holds[h][r].phase="terminal"
ExactBytes(p,s) == \A b \in s.observations:
 b.bytes=M!Reconstruct(p.data,s.observationDefinitions[b.root].recipe)
=============================================================================
