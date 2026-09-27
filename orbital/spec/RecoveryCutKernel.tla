--------------------------- MODULE RecoveryCutKernel ---------------------------
EXTENDS Contracts, Integers
CONSTANT TxNone
RestoreActor == <<"restore",0>>
M == INSTANCE MaterialCore
Recipe == [id |-> "checkpoint-1",base |-> "checkpoint-bytes",code |-> "checkpoint-decoder",
           patches |-> <<>>,interpretation |-> "checkpoint-v1"]
Init(p) == [offer |-> TxNone,data |-> [k \in {} |-> k],pointer |-> TxNone,
            submitted |-> {},mode |-> "live",cut |-> 0,
            heads |-> [a \in p.owners |-> TxNone],rebuilt |-> TxNone,
            fence |-> TxNone,externalFence |-> FALSE,opened |-> FALSE,image |-> TxNone]
Request(s,kind,body) ==
  Transition("cut.submit." \o kind,[s EXCEPT !.submitted=@ \cup {kind}],
     <<Event(kind,RestoreActor,0,"journal.submit",Command(0,kind,kind,body))>>)
Apply(s,c) == CASE c.kind="cut.checkpoint" -> [s EXCEPT !.pointer=c.body]
 [] c.kind="cut.fence" -> [s EXCEPT !.fence=c.body]
 [] OTHER -> s
RECURSIVE Replay(_,_)
Replay(s,commands) == IF commands= <<>> THEN s ELSE Replay(Apply(s,Head(commands)),Tail(commands))
Receive(p,s,e) ==
 CASE e.kind="cut.offer" ->
   {Transition("cut.capture",[s EXCEPT !.offer=e.body],<<>>)}
 [] e.kind="cut.restore" ->
   {Transition("cut.lose-controller-cache",
       [s EXCEPT !.mode=e.body.mode,!.cut=e.body.cut,!.pointer=TxNone,!.fence=TxNone,
                 !.heads=[a \in p.owners |-> TxNone],!.submitted={}],
       [i \in 1..Cardinality(p.owners) |->
         Event(<<"recover-cut",i-1>>, RestoreActor,i-1,"journal.recover",[reason |-> e.body.mode])])}
 [] e.kind="journal.deliver" ->
   {Transition("cut.fold",Apply(s,e.body.command),<<>>)}
 [] e.kind="journal.snapshot" ->
   {Transition("cut.recover-chosen-prefix",
       [Replay(s,e.body.prefix) EXCEPT !.heads[e.body.owner]=e.body.prefix],<<>>)}
 [] e.kind="cut.rebuilt" ->
   {Transition("cut.accept-engine-reconstruction",[s EXCEPT !.rebuilt=e.body],<<>>)}
 [] e.kind="deployment.fenced" ->
   IF e.src="deployment" /\ e.body.lineage=1
   THEN {Transition("cut.external-fence-evidence",[s EXCEPT !.externalFence=TRUE],<<>>)} ELSE {}
 [] e.kind="old.write" ->
   IF s.opened /\ (p.bug="accept-old-lineage" \/ e.body.lineage=s.rebuilt.lineage)
   THEN {Transition("cut.accept-write",[s EXCEPT !.image=e.body.image],<<>>)}
   ELSE {Transition("cut.reject-old-lineage",s,<<>>)}
 [] OTHER -> {}
Actions(p,s) ==
 (IF s.offer#TxNone /\ "checkpoint-bytes" \notin DOMAIN s.data
  THEN {Transition("cut.persist-bytes",[s EXCEPT !.data=@ @@ ("checkpoint-bytes" :>
      [kind |-> "base",interpretation |-> "checkpoint-v1",content |-> <<s.offer.prefix,s.offer.image>>])],<<>>)} ELSE {})
 \cup (IF s.offer#TxNone /\ "checkpoint-decoder" \notin DOMAIN s.data
  THEN {Transition("cut.persist-decoder",[s EXCEPT !.data=@ @@ ("checkpoint-decoder" :>
      [kind |-> "code",interpretation |-> "checkpoint-v1",content |-> "replace-byte-v1"])],<<>>)} ELSE {})
 \cup (IF s.mode="live" /\ s.offer#TxNone /\ "cut.checkpoint" \notin s.submitted /\
          (p.bug="publish-unpersisted" \/ M!WellTyped(s.data,Recipe))
   THEN {Request(s,"cut.checkpoint",[recipe |-> Recipe,cursor |->
          [a \in DOMAIN s.offer.prefix |-> Len(s.offer.prefix[a])+IF p.bug="advance-cursor" /\ a=2 THEN 1 ELSE 0]])} ELSE {})
 \cup (IF s.mode="pitr" /\ s.rebuilt#TxNone /\ "cut.fence" \notin s.submitted
   THEN {Request(s,"cut.fence",[oldLineage |-> 1,lineage |-> 2,cut |-> s.cut,abandoned |-> IF p.bug="omit-abandonment" THEN {} ELSE s.rebuilt.abandoned])} ELSE {})
 \cup (IF s.mode#"live" /\ s.rebuilt#TxNone /\ ~s.opened /\
          (s.mode="normal" \/ (s.fence#TxNone /\ (s.externalFence \/ p.bug="journal-is-fence")))
   THEN {Transition("cut.open",[s EXCEPT !.opened=TRUE,!.image=s.rebuilt.image],
       <<Event("opened",RestoreActor,"application","cut.opened",s.rebuilt)>> \o
       (IF p.bug="reemit-history" THEN
          [i \in 1..Cardinality(s.rebuilt.historical) |->
             Event(<<"historical",i>>,RestoreActor,"sink","external.intent",
                    CHOOSE o \in s.rebuilt.historical:TRUE)] ELSE <<>>))} ELSE {})
MaterialReady(s) == s.pointer#TxNone /\ M!WellTyped(s.data,s.pointer.recipe)
Base(s) == M!Reconstruct(s.data,s.pointer.recipe)
HeadsReady(p,s) == \A a \in p.owners:s.heads[a]#TxNone
PublishedDurable(s) == s.pointer#TxNone => MaterialReady(s)
CursorExact(s) == MaterialReady(s) =>
   \A a \in DOMAIN s.pointer.cursor:s.pointer.cursor[a]=Len(Base(s)[1][a])
=============================================================================
