---------------------------- MODULE EpochFoldKernel ----------------------------
EXTENDS Contracts, Integers
CONSTANT TxNone
K == INSTANCE TxKernel

(* One complete consumer of immutable agreed input batches. Every operation
   reads only this record, fixed parameters and the batch argument. In
   particular there is no shared physical queue, capacity counter, clock or
   peer-consumer state. EpochTransactionFolds checks the commuting product and
   uses this footprint to explore the larger product one consumer at a time. *)
Init(p) == [state |-> K!Init(p.tx),cursor |-> [a \in p.tx.shards |-> 0],epoch |-> 1,
 ready |-> {},outputs |-> [id \in {} |-> TxNone],outputClash |-> FALSE,
 closed |-> [e \in 1..p.last |-> TxNone]]
RECURSIVE Facts(_,_,_), StoreEvents(_,_), EventClash(_,_)
Facts(p,s,es) == IF es= <<>> THEN s ELSE
 IF Head(es).kind="tx.fact"
 THEN Facts(p,(CHOOSE tr \in K!Receive(p.tx,s,Head(es)):TRUE).next,Tail(es))
 ELSE Facts(p,s,Tail(es))
StoreEvents(out,es) == IF es= <<>> THEN out ELSE StoreEvents(out @@ (Head(es).id :> Head(es)),Tail(es))
EventClash(out,es) == IF es= <<>> THEN FALSE ELSE
 (Head(es).id \in DOMAIN out /\ out[Head(es).id]#Head(es)) \/
 EventClash(StoreEvents(out,<<Head(es)>>),Tail(es))
Folded(p,s,tr) == [s EXCEPT !.state=Facts(p,tr.next,tr.emissions),
 !.outputs=StoreEvents(@,tr.emissions),!.outputClash=@ \/ EventClash(s.outputs,tr.emissions)]
Eligible(p,batches,s) ==
 {i \in 1..Len(batches[s.epoch]):
   LET e==batches[s.epoch][i] IN e.index=s.cursor[e.owner]+1}
Chosen(p,batches,s) == CHOOSE i \in Eligible(p,batches,s):
 \A j \in Eligible(p,batches,s):IF p.selector=1 THEN i<=j ELSE i>=j
Deliver(p,batches,s) ==
 IF s.closed[s.epoch]#TxNone \/ Eligible(p,batches,s)={} THEN {} ELSE
 {LET e==batches[s.epoch][i]
     txp==IF p.bug="drop-bound" /\ p.selector=2 THEN [p.tx EXCEPT !.bug="replay-drop-bound"] ELSE p.tx
     tr==K!ReplayCommands(txp,s.state,<<e.command>>)
 IN Transition("fold.deliver",[Folded(p,s,tr) EXCEPT !.cursor[e.owner]=e.index],tr.emissions):
 i \in IF p.anyOrder THEN Eligible(p,batches,s) ELSE {Chosen(p,batches,s)}}
Work(p,s) == UNION {K!ReadActions(p.tx,s.state,t) \cup K!ComputeActions(p.tx,s.state,t) \cup
 K!PublishActions(p.tx,s.state,t):t \in p.tx.transactions}
PhysicalWork(p,s) == UNION {
 (IF t \in s.ready THEN K!ReadActions(p.tx,s.state,t) \cup K!ComputeActions(p.tx,s.state,t) ELSE {}) \cup
 K!PublishActions(p.tx,s.state,t):t \in p.tx.transactions}
Materialize(p,s) == {Transition("fold.materialize",[s EXCEPT !.ready=@ \cup {t}],<<>>):
 t \in {u \in p.tx.transactions \ s.ready:s.state.registered[u]#{} \/ K!ExecutionReady(p.tx,s.state,u)}}
Execute(p,s) == IF s.closed[s.epoch]#TxNone THEN {} ELSE
 {Transition("fold.execute",Folded(p,s,tr),tr.emissions):tr \in PhysicalWork(p,s)}
KindSlot(kind) == CASE kind="begin" -> 1 [] kind="enroll" -> 2 [] kind="grant" -> 3
 [] kind="minimum" -> 4 [] kind="position" -> 5 [] kind="fix" -> 6 [] kind="bound" -> 7
 [] kind="read" -> 8 [] kind="report" -> 9 [] kind="outcome" -> 10 [] kind="decision" -> 11
 [] kind="installed" -> 12 [] OTHER -> 98
OutputSlot(e) == 10000*e.body.tx+(IF e.kind="tx.fact" THEN 100*KindSlot(e.body.kind)+e.body.key ELSE 9999)
CanonicalOutputs(es) == [i \in 1..Cardinality(DOMAIN es) |->
 es[CHOOSE id \in DOMAIN es:Cardinality({other \in DOMAIN es:OutputSlot(es[other])<OutputSlot(es[id])})=i-1]]
Projection(s) == [logical |-> s.state,outputs |-> CanonicalOutputs(s.outputs)]
Close(p,batches,s) == IF s.closed[s.epoch]#TxNone \/ Eligible(p,batches,s)#{} \/
 (IF p.bug="physical-close" THEN PhysicalWork(p,s)#{} ELSE Work(p,s)#{}) THEN {} ELSE
 {Transition("fold.close",[s EXCEPT !.closed[s.epoch]=Projection(s)],<<>>)}
Advance(p,s) == IF s.closed[s.epoch]=TxNone \/ s.epoch=p.last THEN {} ELSE
 {Transition("fold.advance",[s EXCEPT !.epoch=@+1],<<>>)}
Actions(p,batches,s) == Deliver(p,batches,s) \cup Execute(p,s) \cup Materialize(p,s) \cup
 Close(p,batches,s) \cup Advance(p,s)
Done(p,s) == s.closed[p.last]#TxNone
UniqueOutputSlots(s) == /\ ~s.outputClash
 /\ \A a,b \in DOMAIN s.outputs:OutputSlot(s.outputs[a])=OutputSlot(s.outputs[b]) => a=b
=============================================================================
