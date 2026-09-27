-------------------------- MODULE DeliveryKernel --------------------------
EXTENDS Contracts, Integers

\* The application gives canonical identities, coverage and representations.
\* Routes and origin machines do not create contributions or completion facts.
Logical(p,k) == IF "identities" \in DOMAIN p THEN p.identities[k]
               ELSE ToString(<<p.lineage,p.call,k,p.cut,p.interpretation>>)
Context(p) == IF "context" \in DOMAIN p THEN p.context
              ELSE <<p.lineage,p.call,p.cut,p.interpretation>>
ExpectedValue(p,k) == IF "expected" \in DOMAIN p THEN p.expected[k]
                     ELSE IF k=p.child THEN p.inputs[1][p.parent]*2+1 ELSE p.inputs[1][k]*2
Encode(v,form) == IF form="raw" THEN <<v>> ELSE <<v+7>>
Decode(bytes,form) == IF form="raw" THEN bytes[1] ELSE bytes[1]-7
Empty == [open |-> FALSE,value |-> 0,coverage |-> {},expandable |-> FALSE]
Init(p) ==
 [outbox |-> [k \in p.keys |-> Empty],
  custody |-> [r \in p.recipients |-> [id \in {} |-> id]],
  applied |-> [r \in p.recipients |-> [id \in {} |-> id]],
  inbox |-> [r \in p.recipients |-> {}],
  done |-> [k \in p.keys |-> {}],
  expanded |-> {},children |-> [k \in p.keys |-> {}],
  cancelled |-> {},members |-> p.initialMembers,
  cursor |-> [a \in {p.owner} \cup p.recipients |-> 0],
  seen |-> [a \in {p.owner} \cup p.recipients |-> {}],
  up |-> [r \in p.recipients |-> TRUE],restarted |-> {},
  submitted |-> {},sent |-> {},derived |-> {},candidates |-> {},route |-> 1,
  recipientGeneration |-> [r \in p.recipients |-> 1],
  transport |-> {},closed |-> {},completed |-> {},
  checkpoints |-> [r \in p.recipients |-> {}],
  outputs |-> [r \in p.recipients |-> 0],receipts |-> {},conflicts |-> {},
  arrivals |-> {},
  enrollment |-> [r \in p.recipients |-> [keys |-> {},cut |-> 0]],
  joinedComplete |-> {},joinHistory |-> {},restartCuts |-> {},custodyEvidence |-> {},replays |-> {}]
Submit(p,s,owner,id,kind,body) ==
 LET c == Command(owner,id,kind,body)
 IN Transition("delivery-propose",[s EXCEPT !.submitted=@ \cup {c.id}],
       <<Event(<<"submit",c.id>>,owner,owner,"journal.submit",c)>>)

Apply(p,s,c) ==
 IF c.id \in s.seen[c.owner] THEN s ELSE
 LET n == [s EXCEPT !.seen[c.owner]=@ \cup {c.id}]
     b == c.body
 IN CASE c.kind="delivery.open" ->
       IF s.outbox[b.key].open THEN n ELSE
       [n EXCEPT !.outbox[b.key]=[open |-> TRUE,value |-> b.value,
                 coverage |-> b.coverage,expandable |-> b.expandable]]
    [] c.kind="delivery.custody" ->
       [n EXCEPT !.custody[c.owner]=@ @@ (b.id :> b)]
    [] c.kind="delivery.complete" ->
       IF b.id \in DOMAIN s.applied[c.owner] /\ p.bad#"no-app-dedup" THEN n ELSE
       [n EXCEPT !.applied[c.owner]=@ @@ (b.id :> b),
                 !.checkpoints[c.owner]=IF p.bad="split-checkpoint" THEN @ ELSE @ \cup {b.key},
                 !.outputs[c.owner]=IF p.bad="checkpoint-only" THEN @ ELSE @+b.value]
    [] c.kind="delivery.ack" ->
       [n EXCEPT !.done[b.key]=@ \cup {b.recipient}]
    [] c.kind="delivery.expand" ->
       [n EXCEPT !.expanded=@ \cup {b.parent},!.children[b.parent]=b.children,
         !.outbox[p.child]=[open |-> TRUE,value |-> b.value,
                   coverage |-> s.outbox[b.parent].coverage,expandable |-> FALSE]]
    [] c.kind="delivery.cancel" ->
       [n EXCEPT !.cancelled=@ \cup (IF p.bad="cancel-shared" THEN s.members ELSE {b.recipient})]
    [] c.kind="delivery.join" ->
       [n EXCEPT !.members=@ \cup {b.recipient},
         !.enrollment[b.recipient]=[keys |-> b.keys,cut |-> b.cut],
         !.joinHistory=@ \cup {[recipient |-> b.recipient,cut |-> b.cut,keys |-> b.keys,closed |-> s.closed]}]
    [] c.kind="delivery.join-complete" ->
       [n EXCEPT !.joinedComplete=@ \cup {b.recipient}]
    [] OTHER -> n

RECURSIVE Fold(_,_,_)
Fold(p,s,cs) == IF cs= <<>> THEN s ELSE
 Fold(p,IF Head(cs).kind \in {"delivery.custody","delivery.complete"}
        THEN Apply(p,s,Head(cs)) ELSE s,Tail(cs))
Receive(p,s,e) ==
 CASE e.kind="journal.deliver" ->
       IF e.body.index=s.cursor[e.body.owner]+1 /\
          (e.body.owner=p.owner \/ s.up[e.body.owner])
       THEN {Transition("delivery-journal-record",
         [Apply(p,s,e.body.command) EXCEPT !.cursor[e.body.owner]=e.body.index],
          IF e.body.command.kind="delivery.join-complete"
          THEN <<Event(<<"join-release",e.body.command.body.recipient>>,p.owner,p.owner,
                    "delivery.join-release",e.body.command.body)>>
          ELSE IF e.body.command.kind="delivery.cancel"
          THEN <<Event(<<"cancelled",e.body.command.body.recipient>>,p.owner,p.owner,
                    "delivery.cancelled",e.body.command.body)>> ELSE <<>>)}
       ELSE IF e.body.index<=s.cursor[e.body.owner]
            THEN {Transition("delivery-old-record",s,<<>>)} ELSE {}
    [] e.kind="journal.snapshot" ->
       LET r == e.body.owner
           base == [s EXCEPT !.custody[r]=[id \in {} |-> id],
                             !.applied[r]=[id \in {} |-> id],!.checkpoints[r]={},
                             !.outputs[r]=0,!.seen[r]={}]
       IN IF ~s.up[r] /\ e.body.recovery=ToString(<<"recover",r>>)
          THEN {Transition("delivery-replay",
            [Fold(p,base,e.body.prefix) EXCEPT !.cursor[r]=e.body.index,!.up[r]=TRUE,
              !.replays=@ \cup {[recipient |-> r,prefix |-> e.body.prefix]}],
            <<Event(<<"resume",r>>,r,p.owner,"delivery.resume",[recipient |-> r,generation |-> 2])>>)} ELSE {}
    [] e.kind="delivery.resume" ->
       {Transition("recipient-resume-request",
          [s EXCEPT !.recipientGeneration[e.body.recipient]=e.body.generation],<<>>)}
    [] e.kind="delivery.payload" ->
       IF s.up[e.dst]
       THEN LET b == e.body
                logical == IF p.bad="route-identity" THEN ToString(<<b.id,b.route>>) ELSE b.id
                value == IF p.bad="wrong-transform" THEN b.bytes[1] ELSE Decode(b.bytes,b.form)
                item == [id |-> logical,key |-> b.key,value |-> value,context |-> b.context]
                previous == {x \in s.inbox[e.dst]:x.id=logical} \cup
                  (IF logical \in DOMAIN s.custody[e.dst] THEN {s.custody[e.dst][logical]} ELSE {})
                conflict == \E x \in previous:x#item
            IN IF conflict
               THEN {Transition("reject-content-conflict",[s EXCEPT !.conflicts=@ \cup
                      {[recipient |-> e.dst,id |-> logical,received |-> item]}],<<>>)}
               ELSE {Transition("delivery-arrival",[s EXCEPT !.inbox[e.dst]=@ \cup {item},
                     !.arrivals=@ \cup {[recipient |-> e.dst,id |-> logical,route |-> b.route,
                                        form |-> b.form,generation |-> s.recipientGeneration[e.dst]]}],
                  <<Event(<<"transport",e.id>>,e.dst,p.owner,"delivery.transport",
                       [key |-> b.key,recipient |-> e.dst])>>)} ELSE {}
    [] e.kind="delivery.transport" ->
       {Transition("transport-receipt",[s EXCEPT !.transport=@ \cup {e.body}],<<>>)}
    [] e.kind="delivery.processed" ->
       {Transition("completion-receipt",[s EXCEPT !.receipts=@ \cup {e.body}],<<>>)}
    [] e.kind="delivery.intent" ->
       IF e.body.key \in p.keys
       THEN {Submit(p,s,p.owner,<<"intent",e.body.id>>, "delivery.open",e.body)} ELSE {}
    [] e.kind="delivery.derived" ->
       LET b == e.body
           body == [id |-> b.id,key |-> b.key,value |-> b.value,
                    coverage |-> p.initialMembers,expandable |-> p.dynamic /\ b.key=p.parent]
       IN IF b.id=Logical(p,b.key) /\ b.context=Context(p)
          THEN {Transition("receive-derived-intent",[tr.next EXCEPT !.derived=@ \cup {<<b.origin,b.key>>},
                  !.candidates=@ \cup {[origin |-> b.origin,key |-> b.key,value |-> b.value]}],tr.emissions):
                tr \in {Submit(p,s,p.owner,<<"derived",b.origin,b.key>>,"delivery.open",body)}} ELSE {}
    [] e.kind \in {"delivery.release","delivery.join-release","delivery.cancelled"} ->
       {Transition("delivery-retention-notice",s,<<>>)}
    [] OTHER -> {}

\* Fixture origins evaluate their own input. The composed family substitutes
\* independently executed, committed program outboxes through delivery.derived.
\* Conflicting duplicates are detected at a recipient; this is not a Byzantine
\* protocol for undoing a previously published bad first contribution.
Derive(p,s,o,k) ==
 LET id == <<"derive",o,k>>
 IN IF k \in p.baseKeys /\ id \notin s.derived /\
       ~(IF "externalOrigins" \in DOMAIN p THEN p.externalOrigins ELSE FALSE)
    THEN LET value == p.inputs[o][k]*2
             body == [id |-> Logical(p,k),key |-> k,value |-> value,
                      coverage |-> p.initialMembers,expandable |-> p.dynamic /\ k=p.parent]
         IN {Transition("derive-outbox-command",[tr.next EXCEPT !.derived=@ \cup {id},
                 !.candidates=@ \cup {[origin |-> o,key |-> k,value |-> value]}],tr.emissions):
              tr \in {Submit(p,s,p.owner,id,"delivery.open",body)}} ELSE {}
Send(p,s,k,r,form,n,o,value) ==
 LET id == <<"send",k,r,s.route,form,n,o,s.recipientGeneration[r]>>
 IN IF s.outbox[k].open /\ r \in s.members /\ r \notin s.cancelled /\
       ToString(id) \notin s.sent /\ (r \notin s.done[k] \/ n=2)
    THEN LET b == [id |-> Logical(p,k),key |-> k,route |-> s.route,form |-> form,
                  bytes |-> Encode(value,form),
                  context |-> Context(p)]
         IN {Transition("send-derived-contribution",[s EXCEPT !.sent=@ \cup {ToString(id)}],
              <<Event(id,ToString(o),r,"delivery.payload",b)>>)} ELSE {}
Custody(p,s,r,b) ==
 LET id == <<"custody",r,b.id>>
 IN IF s.up[r] /\ b.id \notin DOMAIN s.custody[r] /\ ToString(id) \notin s.submitted
    THEN {Transition(tr.tag,[tr.next EXCEPT !.custodyEvidence=@ \cup
             {[recipient |-> r,id |-> b.id,
               carriers |-> {a \in s.arrivals:a.recipient=r /\ a.id=b.id}]}],tr.emissions):
           tr \in {Submit(p,s,r,id,"delivery.custody",b)}} ELSE {}
Process(p,s,r,id,n) ==
 LET cid == <<"complete",r,id,n>>
 IN IF s.up[r] /\ ToString(cid) \notin s.submitted
    THEN {Submit(p,s,r,cid,"delivery.complete",s.custody[r][id])} ELSE {}
Ack(p,s,r,id,n) ==
 LET b == s.applied[r][id]
     msg == <<"processed",r,id,n>>
 IN IF s.up[r] /\ ToString(msg) \notin s.sent
    THEN {Transition("send-completion-evidence",[s EXCEPT !.sent=@ \cup {ToString(msg)}],
       <<Event(msg,r,p.owner,"delivery.processed",[key |-> b.key,recipient |-> r,
          value |-> b.value,children |-> IF p.dynamic /\ b.key=p.parent THEN {p.child} ELSE {}])>>)} ELSE {}
RecordAck(p,s,b) ==
 LET id == <<"ack",b.key,b.recipient>>
 IN IF ToString(id) \notin s.submitted
    THEN {Submit(p,s,p.owner,id,"delivery.ack",b)} ELSE {}
Expand(p,s,b) ==
 LET id == <<"expand",b.key>>
 IN IF b.children#{} /\ ToString(id) \notin s.submitted
    THEN {Submit(p,s,p.owner,id,"delivery.expand",
         [parent |-> b.key,children |-> b.children,value |-> b.value+1])} ELSE {}
Closed(p,s,k) ==
 /\ s.outbox[k].open
 /\ s.outbox[k].coverage \subseteq s.done[k] \cup s.cancelled
 /\ (~s.outbox[k].expandable \/ k \in s.expanded \/ s.outbox[k].coverage \subseteq s.cancelled \/ p.bad="close-before-child")
 /\ \A child \in s.children[k]:s.outbox[child].coverage \subseteq s.done[child] \cup s.cancelled
Close(p,s,k) ==
 IF k \notin s.closed /\ (Closed(p,s,k) \/
      (p.bad="transport-completion" /\ s.outbox[k].open /\
        s.outbox[k].coverage \subseteq {b.recipient:b \in {t \in s.transport:t.key=k}}))
 THEN {Transition("discharge-output-obligation",[s EXCEPT !.closed=@ \cup {k}],
       <<Event(<<"release",k>>,p.owner,p.owner,"delivery.release",
               [key |-> k,id |-> Logical(p,k),context |-> Context(p)])>>)} ELSE {}
Route(p,s) == IF s.route<p.routes
 THEN {Transition("route-replacement",[s EXCEPT !.route=@+1],<<>>)} ELSE {}
Restart(p,s,r) ==
 IF p.reset /\ r \notin s.restarted
 THEN {Transition("recipient-process-reset",[s EXCEPT !.up[r]=FALSE,
         !.restarted=@ \cup {r},!.inbox[r]={},
         !.restartCuts=@ \cup {[recipient |-> r,applied |-> DOMAIN s.applied[r],
                               custody |-> DOMAIN s.custody[r],done |-> s.done,route |-> s.route]}],
        <<Event(<<"recover",r>>,r,r,"journal.recover",[recovery |-> 1])>>)} ELSE {}
Join(p,s,r) ==
 LET id == <<"join",r>>
 IN IF p.join /\ r \notin s.members /\ ToString(id) \notin s.submitted
    THEN {Submit(p,s,p.owner,id,"delivery.join",[recipient |-> r,cut |-> p.cut,
           keys |-> p.baseKeys \cup (IF p.dynamic THEN {p.child} ELSE {})])} ELSE {}
JoinComplete(p,s,r) ==
 LET id == <<"joined-complete",r>>
 IN IF r \in s.members \ p.initialMembers /\ r \notin s.joinedComplete /\
       ToString(id) \notin s.submitted /\
       \A k \in s.enrollment[r].keys:r \in s.done[k] \cup s.cancelled
    THEN {Submit(p,s,p.owner,id,"delivery.join-complete",
           [recipient |-> r,cut |-> s.enrollment[r].cut,keys |-> s.enrollment[r].keys])} ELSE {}
Cancel(p,s,r) ==
 LET id == <<"cancel",r>>
 IN IF p.cancel /\ r \in s.members /\ r \in p.cancelRecipients /\ r \notin s.cancelled /\ ToString(id) \notin s.submitted
    THEN {Submit(p,s,p.owner,id,"delivery.cancel",[recipient |-> r])} ELSE {}
Actions(p,s) ==
 Route(p,s)
 \cup UNION {Derive(p,s,o,k):o \in p.origins,k \in p.baseKeys}
 \cup UNION {Send(p,s,c.key,r,f,n,c.origin,c.value):c \in s.candidates,
          r \in p.recipients,f \in p.forms,n \in 1..p.duplicates}
 \cup (IF p.dynamic THEN UNION {Send(p,s,p.child,r,f,n,0,s.outbox[p.child].value):
          r \in p.recipients,f \in p.forms,n \in 1..p.duplicates} ELSE {})
 \cup UNION {UNION {Custody(p,s,r,b):b \in s.inbox[r]}:r \in p.recipients}
 \cup UNION {UNION {Process(p,s,r,id,n):id \in DOMAIN s.custody[r],n \in 1..p.duplicates}:r \in p.recipients}
 \cup UNION {UNION {Ack(p,s,r,id,n):id \in DOMAIN s.applied[r],n \in 1..p.duplicates}:r \in p.recipients}
 \cup UNION {RecordAck(p,s,b) \cup Expand(p,s,b):b \in s.receipts}
 \cup UNION {Close(p,s,k):k \in p.keys}
 \cup UNION {Restart(p,s,r) \cup Join(p,s,r) \cup JoinComplete(p,s,r) \cup Cancel(p,s,r):r \in p.recipients}

DerivedAgreement(p,s) == \A a,b \in s.candidates:a.key=b.key => a.value=b.value
Coverage(p,s) == \A k \in s.closed:
 /\ s.outbox[k].coverage \subseteq s.done[k] \cup s.cancelled
 /\ (s.outbox[k].expandable => (s.outbox[k].coverage \subseteq s.cancelled \/
      (k \in s.expanded /\ s.outbox[p.child].coverage \subseteq s.done[p.child] \cup s.cancelled)))
Canonical(p,s) == \A r \in p.recipients: \A id \in DOMAIN s.applied[r]:
 LET b == s.applied[r][id]
 IN /\ id=Logical(p,b.key)
    /\ b.value=ExpectedValue(p,b.key)
    /\ b.context=Context(p)
Checkpoints(p,s) == \A r \in p.recipients:
 s.checkpoints[r]={s.applied[r][id].key:id \in DOMAIN s.applied[r]}
RECURSIVE ExpectedSum(_,_,_)
ExpectedSum(p,keys,n) == IF n=0 THEN 0 ELSE
 ExpectedSum(p,keys,n-1)+(IF n \in keys THEN
     ExpectedValue(p,n) ELSE 0)
EffectMultiplicity(p,s) == \A r \in p.recipients:
 LET keys == {s.applied[r][id].key:id \in DOMAIN s.applied[r]}
 IN /\ s.outputs[r]=ExpectedSum(p,keys,p.child)
    /\ Cardinality(DOMAIN s.applied[r])=Cardinality(keys)
JoinedCoverage(p,s) == \A r \in s.joinedComplete:
 /\ s.enrollment[r].cut=p.cut
 /\ \A k \in s.enrollment[r].keys:r \in s.cancelled \/ Logical(p,k) \in DOMAIN s.applied[r]
CancellationAuthority(p,s) == s.cancelled \subseteq p.cancelRecipients
=============================================================================
