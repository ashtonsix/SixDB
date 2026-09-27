-------------------------- MODULE IndependentTransactions --------------------------
EXTENDS Contracts, TxFixtures, Integers
CONSTANTS TxNone, Bug
K == INSTANCE TxKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
P == [Params("pair","none",FALSE) EXCEPT
 !.writes[1]={1},!.parts[1]={1},!.reads[1]={2},!.firstRead[1]=2,!.program[1]="increment",
 !.writes[2]={4},!.parts[2]={1},!.reads[2]={},!.program[2]="put",!.value[2]=7,!.causal[2]=7]
JP == [owners |-> P.shards,actors |-> {K!Fold(a):a \in P.shards},
 subscribers |-> [a \in P.shards |-> {K!Fold(a)}],initialConfig |-> [a \in P.shards |-> 1]]
(* Inert sum tags permit TLC to sort unlike payload types. They affect no
   application field, identity, evidence requirement or transport scheduling. *)
Wire(e) == [a |-> ToString(<<e.kind,IF e.kind="tx.fact" THEN e.body.kind ELSE "">>)] @@
 (IF e.kind="journal.submit" THEN [e EXCEPT !.body=[a |-> @.kind] @@ @] ELSE e)
WireEvents(es) == {Wire(es[i]):i \in 1..Len(es)}
VARIABLES tx,journal,network,held,client
vars == <<tx,journal,network,held,client>>
Init == /\ tx=K!Init(P) /\ journal=J!Init(JP) /\ network={}
 /\ held=TxNone /\ client={}
Remote(e) == e.kind="tx.fact" /\ e.body.tx=1 /\ e.body.kind="read" /\ e.body.key=2
Choices == {tr \in K!Actions(P,tx):tr.tag#"tx.submit.begin" \/
 Head(tr.emissions).body.body.tx#2 \/ held#TxNone}
KernelStep ==
 /\ (Bug#"shard-barrier" \/ held=TxNone)
 /\ \E tr \in Choices:tx'=tr.next /\ network'=network \cup WireEvents(tr.emissions)
 /\ UNCHANGED <<journal,held,client>>
JournalStep == \E tr \in J!Actions(JP,journal):
 /\ journal'=tr.next /\ network'=network \cup WireEvents(tr.emissions)
 /\ UNCHANGED <<tx,held,client>>
Deliver == \E e \in network:
 \/ /\ Remote(e) /\ held=TxNone
    /\ held'=e /\ network'=network \ {e} /\ UNCHANGED <<tx,journal,client>>
 \/ /\ e.kind \in {"journal.submit","journal.recover"}
    /\ \E tr \in J!Receive(JP,journal,e):
       /\ journal'=tr.next /\ network'=(network \ {e}) \cup WireEvents(tr.emissions)
    /\ UNCHANGED <<tx,held,client>>
 \/ /\ (e.kind="journal.deliver" \/ (e.kind="tx.fact" /\ ~Remote(e)))
    /\ \E tr \in K!Receive(P,tx,e):
       /\ tx'=tr.next /\ network'=(network \ {e}) \cup WireEvents(tr.emissions)
    /\ UNCHANGED <<journal,held,client>>
 \/ /\ e.kind="tx.published"
    /\ client'=client \cup {e.body} /\ network'=network \ {e}
    /\ UNCHANGED <<tx,journal,held>>
(* The actual remote read fact is never delivered. Local service remains fair;
   the unavailable remote service has no fairness promise. No heartbeat or
   unrelated loop stands in for completion of the independent transaction. *)
Next == KernelStep \/ JournalStep \/ Deliver \/ (held#TxNone /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(KernelStep) /\ WF_vars(JournalStep) /\ WF_vars(Deliver)
IndependentDone == \E result \in client:result.tx=2 /\ result.decision="commit" /\ result.outcome.result=7
IndependentCompletes == <>IndependentDone
RemoteStillPending == held#TxNone =>
 /\ Remote(held) /\ held.src=K!Fold(2) /\ held.dst=K!Driver(1)
 /\ tx.readCut[1][2]=tx.position[1] /\ tx.position[1]>0
 /\ tx.inputs[1][2]=TxNone /\ tx.private[1]=TxNone /\ tx.outcome[1]=TxNone /\ 1 \notin tx.published
IndependentMeaning ==
 /\ O!ObservedAtPosition(P,tx) /\ O!ResultSemantics(P,tx) /\ O!PublishedSemantics(P,tx)
 /\ (IndependentDone => held#TxNone /\ tx.position[2]>P.causal[2] /\
        \E v \in tx.versions:v.tx=2 /\ v.key=4 /\ v.value=7)
NoIndependentCompletion == ~IndependentDone
=============================================================================
