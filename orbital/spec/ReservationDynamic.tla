------------------------- MODULE ReservationDynamic -------------------------
EXTENDS Naturals, FiniteSets, Sequences
CONSTANTS Policy, Bug
K == INSTANCE ReservationKernel
Tx == 0..2
Writes == [t \in Tx |-> CASE t=0 -> {1} [] t=1 -> {1,2} [] OTHER -> {2}]
Fresh == [state |-> K!Initial,seen |-> {},outputs |-> <<>>,cursor |-> 0]
VARIABLES authority,history,arrived,retired,replicas,reset,duplicate,sawCancelReset
vars == <<authority,history,arrived,retired,replicas,reset,duplicate,sawCancelReset>>
Init == /\ authority=Fresh /\ history= <<>> /\ arrived={} /\ retired={}
 /\ replicas=[r \in {1,2}|->Fresh] /\ reset=FALSE /\ duplicate=FALSE /\ sawCancelReset=FALSE
Cmd(kind,t) == [id |-> <<kind,t>>,kind |-> kind,tx |-> t]
Apply(s,c,drop) ==
 IF c.id \in s.seen THEN [s EXCEPT !.cursor=@+1]
 ELSE LET tr==IF drop /\ c.kind="cancel" THEN [next |-> s.state,outputs |-> <<>>]
              ELSE K!Fold(Policy,Writes,s.state,c.kind,c.tx)
      IN [s EXCEPT !.state=tr.next,!.seen=@ \cup {c.id},!.outputs=@ \o tr.outputs,!.cursor=@+1]
AppendRecord(kind,t) ==
 /\ CASE kind="enqueue" -> t \notin arrived
    [] kind="fix" -> t \in authority.state.held
    [] OTHER -> t \in K!Items(authority.state.order)
 /\ LET c==Cmd(kind,t) IN /\ authority'=Apply(authority,c,FALSE) /\ history'=Append(history,c)
 /\ arrived'=IF kind="enqueue" THEN arrived \cup {t} ELSE arrived
 /\ retired'=IF kind="enqueue" THEN retired ELSE retired \cup {t}
 /\ UNCHANGED <<replicas,reset,duplicate,sawCancelReset>>
Duplicate == /\ ~duplicate /\ history# <<>>
 /\ \E i \in 1..Len(history):
       /\ history'=Append(history,history[i]) /\ authority'=Apply(authority,history[i],FALSE)
 /\ duplicate'=TRUE /\ UNCHANGED <<arrived,retired,replicas,reset,sawCancelReset>>
AuthorDone == retired=Tx /\ duplicate
Author == /\ ~AuthorDone
 /\ ((\E t \in Tx: \E kind \in {"enqueue","fix","cancel"}:AppendRecord(kind,t)) \/ Duplicate)
Deliver(r) == /\ AuthorDone /\ (r=1 \/ replicas[1].cursor=Len(history))
 /\ replicas[r].cursor<Len(history)
 /\ replicas'=[replicas EXCEPT ![r]=Apply(@,history[@.cursor+1],r=2 /\ reset /\ Bug="drop-cancel")]
 /\ UNCHANGED <<authority,history,arrived,retired,reset,duplicate,sawCancelReset>>
Lose == /\ ~reset /\ replicas[2].cursor>0
 /\ sawCancelReset'=(\E i \in 1..replicas[2].cursor:history[i].kind="cancel")
 /\ replicas'=[replicas EXCEPT ![2]=Fresh] /\ reset'=TRUE
 /\ UNCHANGED <<authority,history,arrived,retired,duplicate>>
Done == retired=Tx /\ \A r \in {1,2}:replicas[r].cursor=Len(history)
Next == Author \/ (\E r \in {1,2}:Deliver(r)) \/ Lose \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Author) /\ (\A r \in {1,2}:WF_vars(Deliver(r)))
Safety == K!Sound(Writes,authority.state) /\ \A r \in {1,2}:K!Sound(Writes,replicas[r].state)
Closed == K!Eligible(Policy,Writes,authority.state)={} /\
 \A r \in {1,2}:K!Eligible(Policy,Writes,replicas[r].state)={}
Agreement == replicas[1].cursor=replicas[2].cursor => replicas[1]=replicas[2]
Recovered == Done => \A r \in {1,2}:replicas[r]=authority
Retirement == retired \cap K!Items(authority.state.order)={}
Completes == <>Done
NoCancelReplay == ~(Done /\ reset /\ sawCancelReset)
(* This finite workload explores actual input order and cache-loss cuts. The
   authority history is an abstract chosen command stream, not a new Paxos model.
   Each request arrives once; one exact duplicate and one consumer reset vary.
   Replaying a cancellation cannot restore its old queue/holder protection.
   The author never reads a consumer. Consumers never reply to the author or
   read one another: they use only their own record and the immutable prefix.
   Finish authoring then serialize the two local consumers to preserve each
   possible local trace/final pair without multiplying commuting interleavings.
   There are no shared capacity, clock, network, or feedback claims here. *)
=============================================================================
