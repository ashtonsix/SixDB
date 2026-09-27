------------------------- MODULE ReservationReplay -------------------------
EXTENDS Naturals, FiniteSets, Sequences
CONSTANTS Policy, Scene, Bug, Reset
K == INSTANCE ReservationKernel
Writes == [t \in 0..9 |-> CASE t=0 -> {1} [] t \in {1,9} -> {1,2}
           [] OTHER -> {2}]
CompeteWrites == [Writes EXCEPT ![1]={2}]
W == IF Scene="compete" THEN CompeteWrites ELSE Writes
Cmd(id,kind,t) == [id |-> id,kind |-> kind,tx |-> t]
Commands == IF Scene="compete" THEN
 <<Cmd(1,"enqueue",0),Cmd(2,"enqueue",1),Cmd(3,"enqueue",9),
   Cmd(4,"enqueue",8),Cmd(5,"enqueue",2),Cmd(6,"fix",1),Cmd(4,"enqueue",8)>>
 ELSE <<Cmd(1,"enqueue",0),Cmd(2,"enqueue",1),Cmd(3,"enqueue",2),
        Cmd(4,"fix",0),Cmd(3,"enqueue",2)>>
Fresh == [state |-> K!Initial,cursor |-> 0,seen |-> {},outputs |-> <<>>,reset |-> FALSE]
VARIABLE replicas
vars == <<replicas>>
Init == replicas=[r \in {1,2} |-> Fresh]
RECURSIVE WrongClose(_)
WrongClose(s) == IF K!Eligible(Policy,W,s)={} THEN [next |-> s,outputs |-> <<>>]
 ELSE LET t==CHOOSE x \in K!Eligible(Policy,W,s):\A y \in K!Eligible(Policy,W,s):x<=y
          g==K!Grant(W,s,t) rest==WrongClose(g.next)
      IN [next |-> rest.next,outputs |-> g.outputs \o rest.outputs]
Apply(r,s,c) ==
 IF c.id \in s.seen THEN [s EXCEPT !.cursor=@+1]
 ELSE LET raw==K!Raw(s.state,c.kind,c.tx)
          closure==IF r=2 /\ Bug="deferred" /\ c.id=3
                   THEN [next |-> raw,outputs |-> <<>>]
                   ELSE IF r=2 /\ Bug="id-order" THEN WrongClose(raw)
                   ELSE K!Close(Policy,W,raw)
      IN [s EXCEPT !.state=closure.next,!.cursor=@+1,!.seen=@ \cup {c.id},
          !.outputs=@ \o <<K!Event(c.kind,c.tx,W)>> \o closure.outputs]
Deliver(r) == /\ replicas[r].cursor<Len(Commands)
 /\ replicas'=[replicas EXCEPT ![r]=Apply(r,@,Commands[@.cursor+1])]
Lose(r) == /\ Reset /\ r=2 /\ ~replicas[r].reset /\ replicas[r].cursor=3
 /\ replicas'=[replicas EXCEPT ![r]=[Fresh EXCEPT !.reset=TRUE]]
Done == \A r \in {1,2}:replicas[r].cursor=Len(Commands)
Next == (\E r \in {1,2}:Deliver(r) \/ Lose(r)) \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ (\A r \in {1,2}:WF_vars(Deliver(r)))
Projection(s) == [state |-> s.state,seen |-> s.seen,outputs |-> s.outputs]
Deterministic == Done => Projection(replicas[1])=Projection(replicas[2])
Safety == \A r \in {1,2}:K!Sound(W,replicas[r].state)
ExpectedWinner == Done =>
 replicas[1].state.held=(IF Scene="compete"
   THEN IF Policy="ordered" THEN {0} ELSE {0,8}
   ELSE IF Policy="ordered" THEN {1} ELSE {2})
Completes == <>Done
NoReplay == ~(Done /\ replicas[2].reset)
=============================================================================
