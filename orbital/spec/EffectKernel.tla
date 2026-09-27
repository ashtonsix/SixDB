---------------------------- MODULE EffectKernel ----------------------------
EXTENDS Contracts, Integers

Init(p) == [intents |-> [id \in {} |-> id],received |-> {},
 submitted |-> {},sent |-> {},seen |-> {},cursor |-> 0,up |-> TRUE,incarnation |-> 1,
 sink |-> [id \in {} |-> id],applications |-> [id \in {} |-> id],acks |-> {},
 restored |-> FALSE,lineage |-> "original",abandoned |-> {},unknown |-> {},
 reset |-> FALSE,lostReply |-> FALSE,wasAppliedBeforeReset |-> FALSE]
Submit(p,s,id,kind,body) ==
 LET c == Command(p.owner,id,kind,body)
 IN Transition("effect-propose",[s EXCEPT !.submitted=@ \cup {c.id}],
       <<Event(<<"submit",c.id>>,p.actor,p.owner,"journal.submit",c)>>)
Apply(p,s,c) ==
 IF c.id \in s.seen THEN s ELSE
 LET n == [s EXCEPT !.seen=@ \cup {c.id}]
     b == c.body
 IN CASE c.kind="effect.intent" ->
       IF b.id \in DOMAIN s.intents THEN n ELSE
       [n EXCEPT !.intents=@ @@ (b.id :> [value |-> b.value,origin |-> b.id,cut |-> b.cut,
                    phase |-> "pending",attempt |-> 0,driver |-> 0])]
    [] c.kind="effect.dispatch" ->
       IF s.intents[b.id].phase \in {"pending","inflight"}
       THEN [n EXCEPT !.intents[b.id].phase="inflight",!.intents[b.id].attempt=b.attempt,!.intents[b.id].driver=b.driver] ELSE n
    [] c.kind="effect.ack" ->
       [n EXCEPT !.intents[b.id].phase="acknowledged"]
    [] c.kind="effect.unknown" ->
       [n EXCEPT !.intents[b.id].phase="unknown",!.unknown=@ \cup {b.id}]
    [] OTHER -> n
RECURSIVE Replay(_,_,_)
Replay(p,s,cs) == IF cs= <<>> THEN s ELSE
 Replay(p,IF Head(cs).kind \in {"effect.intent","effect.dispatch","effect.ack","effect.unknown"}
          THEN Apply(p,s,Head(cs)) ELSE s,Tail(cs))
Receive(p,s,e) ==
 CASE e.kind="tx.published" ->
       IF e.body.decision="commit"
       THEN {Transition("committed-effect-obligation",[s EXCEPT !.received=@ \cup
          {[id |-> ToString(x.id),value |-> x.value,cut |-> e.body.position]:x \in Elements(e.body.outcome.outbox)}],<<>>)} ELSE {}
    [] e.kind="journal.deliver" ->
       IF s.up /\ e.body.index=s.cursor+1
       THEN {Transition("effect-journal-record",[Apply(p,s,e.body.command) EXCEPT !.cursor=e.body.index],<<>>)}
       ELSE IF e.body.index<=s.cursor THEN {Transition("old-effect-record",s,<<>>)} ELSE {}
    [] e.kind="journal.snapshot" ->
       IF ~s.up /\ e.body.recovery=ToString(<<"effect-recovery",s.incarnation>>)
       THEN LET base == [s EXCEPT !.intents=[id \in {} |-> id],!.seen={}]
            IN {Transition("effect-owner-replayed",[Replay(p,base,e.body.prefix) EXCEPT
                  !.up=TRUE,!.cursor=e.body.index,!.submitted={}],<<>>)} ELSE {}
    [] e.kind="effect.apply" ->
       LET b == e.body
           fresh == b.wireId \notin DOMAIN s.sink
           count == IF b.origin \in DOMAIN s.applications THEN s.applications[b.origin] ELSE 0
       IN {Transition("external-sink-application",
            [s EXCEPT !.sink=@ @@ (b.wireId :> b.value),
             !.applications=(b.origin :> (count+(IF ~p.dedup \/ fresh THEN 1 ELSE 0))) @@ @],
           <<Event(<<"sink-reply",e.id>>,"sink",p.actor,"effect.reply",[id |-> b.origin])>>)}
    [] e.kind="effect.reply" ->
       {Transition("receive-effect-reply",[s EXCEPT !.acks=@ \cup {e.body.id}],<<>>)}
    [] e.kind="cut.opened" ->
       {Transition("restore-effect-context",[s EXCEPT !.restored=TRUE,!.lineage=e.body.lineage,
          !.abandoned=@ \cup {ToString(o.id):o \in e.body.abandoned}],<<>>)}
    [] OTHER -> {}
Intent(p,s,b) ==
 LET id == <<"intent",b.id>>
 IN IF s.up /\ b.id \notin DOMAIN s.intents /\ ToString(id) \notin s.submitted
    THEN {Submit(p,s,id,"effect.intent",b)} ELSE {}
Dispatch(p,s,id) ==
 LET i == s.intents[id]
     attempt == i.attempt+1
     cid == <<"dispatch",id,attempt>>
 IN IF s.up /\ (~p.restore \/ ~s.reset \/ s.restored) /\ id \notin s.abandoned /\
       (i.phase="pending" \/ (i.phase="inflight" /\ s.reset /\
         (p.dedup \/ p.bad="retry-unknown") /\
         i.driver#s.incarnation)) /\ ToString(cid) \notin s.submitted
    THEN {Submit(p,s,cid,"effect.dispatch",[id |-> id,attempt |-> attempt,driver |-> s.incarnation])} ELSE {}
Send(p,s,id) ==
 LET i == s.intents[id]
     msg == <<"send-effect",id,i.attempt>>
 IN IF s.up /\ (~p.restore \/ ~s.reset \/ s.restored) /\ i.phase="inflight" /\ i.driver=s.incarnation /\ id \notin s.abandoned /\ ToString(msg) \notin s.sent
    THEN LET wireId == IF p.bad="new-restore-identity" /\ s.restored
                      THEN ToString(<<id,s.lineage>>) ELSE id
         IN {Transition("send-external-effect",[s EXCEPT !.sent=@ \cup {ToString(msg)}],
             <<Event(msg,p.actor,"sink","effect.apply",
                [wireId |-> wireId,origin |-> id,value |-> i.value])>>)} ELSE {}
Acknowledge(p,s,id) ==
 \* An old reply still acknowledges this immutable logical effect and value;
 \* driver replacement does not create a different sink operation.
 LET cid == <<"ack",id>>
 IN IF s.up /\ id \in s.acks /\ s.intents[id].phase#"acknowledged" /\ ToString(cid) \notin s.submitted
    THEN {Submit(p,s,cid,"effect.ack",[id |-> id])} ELSE {}
Unknown(p,s,id) ==
 LET cid == <<"unknown",id>>
 IN IF s.up /\ s.reset /\ ~p.dedup /\ s.intents[id].phase="inflight" /\
       s.intents[id].driver#s.incarnation /\
       id \notin s.acks /\ ToString(cid) \notin s.submitted
    THEN {Submit(p,s,cid,"effect.unknown",[id |-> id])} ELSE {}
Reset(p,s) ==
 IF p.reset /\ ~s.reset /\ (\E id \in DOMAIN s.intents:s.intents[id].phase="inflight")
 THEN {Transition("effect-owner-loss",[s EXCEPT !.up=FALSE,!.reset=TRUE,!.incarnation=@+1,!.sent={},!.acks={},
         !.wasAppliedBeforeReset=(\E id \in DOMAIN s.applications:s.applications[id]>0)],
       <<Event(<<"effect-recovery",s.incarnation+1>>,p.actor,p.owner,"journal.recover",[recovery |-> s.incarnation+1])>>)} ELSE {}
Actions(p,s) == Reset(p,s) \cup UNION {Intent(p,s,b):b \in s.received} \cup
 UNION {Dispatch(p,s,id) \cup Send(p,s,id) \cup Acknowledge(p,s,id) \cup Unknown(p,s,id):id \in DOMAIN s.intents}
AtMostOnce(p,s) == \A id \in DOMAIN s.applications:s.applications[id]<=1
CommittedOnly(p,s) == \A id \in DOMAIN s.applications: \E b \in s.received:b.id=id
Resolved(p,s) == DOMAIN s.intents#{} /\
 \A id \in DOMAIN s.intents:s.intents[id].phase \in {"acknowledged","unknown"} \/ id \in s.abandoned
=============================================================================
