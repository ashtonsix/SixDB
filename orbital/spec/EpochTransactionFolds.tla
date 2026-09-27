------------------------- MODULE EpochTransactionFolds -------------------------
EXTENDS Contracts, TxFixtures, Integers
CONSTANTS TxNone, Bug, LastEpoch, Mismatch, RemoteRead, Reduced, Small, AnyOrder
K == INSTANCE TxKernel
O == INSTANCE TransactionOracle
F == INSTANCE EpochFoldKernel
B == Params("basic","none",FALSE)
FullP == [B EXCEPT !.checked={1}, !.mismatch=Mismatch,!.bug=IF Bug="self-source" THEN Bug ELSE "none",
     !.reads[1]={1},!.program[1]="increment",
     !.reads[2]=IF RemoteRead THEN {1,2} ELSE @,!.program[2]=IF RemoteRead THEN "sum" ELSE @,
     !.writes[3]={4},!.reads[3]={},!.program[3]="put",!.parts[3]={1},!.value[3]=7]
P == IF Small THEN [FullP EXCEPT !.transactions={1}] ELSE FullP
Consumers == {1,2}
FP(r) == [tx |-> P,last |-> LastEpoch,bug |-> Bug,selector |-> r,anyOrder |-> AnyOrder]
EmptyLog == [a \in P.shards |-> <<>>]
VARIABLE run
vars == <<run>>
Init == run=[author |-> K!Init(P),mail |-> <<>>,log |-> EmptyLog,order |-> <<>>,
  authorEpoch |-> 1,agreed |-> [e \in 1..LastEpoch |-> TxNone],frozen |-> FALSE,
  replicas |-> [r \in Consumers |-> F!Init(FP(r))]]

RECURSIVE Facts(_,_)
Facts(s,es) == IF es= <<>> THEN s ELSE
 IF Head(es).kind="tx.fact"
 THEN Facts((CHOOSE tr \in K!Receive(P,s,Head(es)):TRUE).next,Tail(es))
 ELSE Facts(s,Tail(es))
Forward(es) == SelectSeq(es,LAMBDA e:e.kind#"tx.fact")
AuthorChoices == {tr \in K!Actions(P,run.author):
 tr.tag#"tx.submit.report" \/
 (run.authorEpoch=LastEpoch /\
  Head(tr.emissions).body.body.data.request=
    (IF Mismatch /\ Head(tr.emissions).body.body.key=2 THEN "b" ELSE "a"))}
Weight(tr) == IF tr.emissions= <<>> THEN 0 ELSE
 IF Head(tr.emissions).kind="journal.submit" THEN 100*Head(tr.emissions).body.body.tx+
   Head(tr.emissions).body.body.key ELSE 1000
AuthorStep ==
 /\ ~run.frozen
 /\ IF run.mail# <<>>
    THEN LET e==Head(run.mail) IN
      IF e.kind="journal.submit"
      THEN LET a==e.dst
               folded==K!ReplayCommands(P,run.author,<<e.body>>)
               entry==[owner |-> a,index |-> Len(run.log[a])+1,command |-> e.body]
           IN run'=[run EXCEPT !.author=Facts(folded.next,folded.emissions),
                  !.mail=Tail(@) \o Forward(folded.emissions),
                  !.log[a]=Append(@,e.body),!.order=Append(@,entry)]
      ELSE run'=[run EXCEPT !.mail=Tail(@)]
    ELSE IF AuthorChoices#{}
         THEN LET tr==CHOOSE a \in AuthorChoices: \A b \in AuthorChoices:Weight(a)<=Weight(b)
              IN run'=[run EXCEPT !.author=Facts(tr.next,tr.emissions),!.mail=Forward(tr.emissions)]
         ELSE /\ run.authorEpoch<LastEpoch \/ run.author.published=P.transactions
              /\ run'=[run EXCEPT !.agreed[run.authorEpoch]=run.order,
                 !.frozen=run.authorEpoch=LastEpoch,
                 !.authorEpoch=IF @<LastEpoch THEN @+1 ELSE @]

(* Two selected cross-owner arrival orders, with all independently enabled
   local reads, computations, publication and physical materialization. The
   large family uses an independence reduction: immutable batches and pure
   per-consumer Actions mean any interleaving commutes to consumer 1 followed
   by consumer 2, preserving each local trace and every pair of closed results.
   Small unreduced cases check the product's step/commutation correspondence. *)
Local(r,rs) == F!Actions(FP(r),run.agreed,rs[r])
EnabledConsumer(r) == ~Reduced \/ r=1 \/ F!Done(FP(1),run.replicas[1])
Step(r) ==
 /\ run.frozen /\ EnabledConsumer(r)
 /\ \E tr \in Local(r,run.replicas):run'=[run EXCEPT !.replicas[r]=tr.next]
Done == \A r \in Consumers:F!Done(FP(r),run.replicas[r])
Next == AuthorStep \/ (\E r \in Consumers:Step(r)) \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(AuthorStep) /\ \A r \in Consumers:WF_vars(Step(r))
Completes == <>Done
(* The selected transitions themselves remain enabled after the other
   consumer's step, and their exact successor records commute. Emissions are
   part of each transition and of its canonical logical output, never hidden. *)
ProductCorrespondence == run.frozen =>
 \A a \in Local(1,run.replicas),b \in Local(2,run.replicas):
 LET left==[run.replicas EXCEPT ![1]=a.next]
     right==[run.replicas EXCEPT ![2]=b.next]
 IN /\ b \in Local(2,left) /\ a \in Local(1,right)
    /\ [left EXCEPT ![2]=b.next]=[right EXCEPT ![1]=a.next]
ClosedAgreement == \A e \in 1..LastEpoch:
 run.replicas[1].closed[e]#TxNone /\ run.replicas[2].closed[e]#TxNone =>
 run.replicas[1].closed[e]=run.replicas[2].closed[e]
UniqueOutputSlots == \A r \in Consumers:F!UniqueOutputSlots(run.replicas[r])
LogicalClosure == \A r \in Consumers:
 run.replicas[r].closed[run.replicas[r].epoch]#TxNone => F!Work(FP(r),run.replicas[r])={}
(* Reference history is the actual agreed author history, checked separately by
   the serial interpreter. A remote committed version can arrive before this
   consumer folds the owner's decision, so local knowledge is not the oracle. *)
SerialMeaning ==
 /\ O!ObservedAtPosition(P,run.author) /\ O!ResultSemantics(P,run.author)
 /\ \A r \in Consumers:
     /\ \A ob \in run.replicas[r].state.observations:
          ob.value=O!ExpectedInput(P,run.author,ob.tx,ob.key)
     /\ \A t \in P.transactions:
          LET inputs==[k \in P.reads[t] |-> O!ExpectedInput(P,run.author,t,k)]
              s==run.replicas[r].state
          IN /\ (s.private[t]#TxNone /\ s.private[t].status="ok" =>
                   s.private[t].effects=O!ProgramEffects(P,t,inputs) /\
                   s.private[t].result=O!ProgramResult(P,t,inputs))
             /\ (s.outcome[t]#TxNone /\ s.outcome[t].status="ok" =>
                   s.outcome[t].effects=O!ProgramEffects(P,t,inputs) /\
                   s.outcome[t].result=O!ProgramResult(P,t,inputs))
IndependentPublication == \A r \in Consumers: \A e \in 1..(LastEpoch-1):
 run.replicas[r].closed[e]#TxNone => run.replicas[r].closed[e].logical.published=(IF Small THEN {} ELSE {3})
NoComplete == ~Done
NoPhysicalSkew == ~(run.replicas[1].closed[1]#TxNone /\ run.replicas[2].closed[1]=TxNone /\
                    1 \notin run.replicas[2].ready)
NoResumption == ~(Done /\ run.replicas[1].closed[1].logical.private[1]#TxNone /\
   run.replicas[1].closed[1].logical.position=run.replicas[1].closed[LastEpoch].logical.position /\
   run.replicas[1].closed[LastEpoch].logical.published=P.transactions)
=============================================================================
