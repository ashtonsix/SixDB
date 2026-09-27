------------------------- MODULE ReservationShapes -------------------------
EXTENDS Naturals, FiniteSets, Sequences
CONSTANTS Policy, Scene
K == INSTANCE ReservationKernel
W == [t \in 0..6 |->
 IF Scene="chain" THEN CASE t=0 -> {0} [] t=1 -> {0,1} [] t=2 -> {1,2}
  [] t=3 -> {2,3} [] t=4 -> {3} [] t=5 -> {4} [] OTHER -> {0}
 ELSE CASE t=0 -> {0} [] t=1 -> {0,1,2} [] t=2 -> {1}
  [] t=3 -> {2} [] t=4 -> {3} [] t=5 -> {0} [] OTHER -> {1}]
Cmd(kind,t) == [kind |-> kind,tx |-> t]
Commands == CASE Scene="chain" -> [i \in 1..7 |-> Cmd("enqueue",i-1)]
 [] Scene="younger" -> <<Cmd("enqueue",0),Cmd("enqueue",1),Cmd("enqueue",2),Cmd("fix",0),Cmd("enqueue",3),Cmd("enqueue",4)>>
 [] Scene="cancel" -> <<Cmd("enqueue",0),Cmd("enqueue",1),Cmd("enqueue",2),Cmd("cancel",1),Cmd("fix",0),Cmd("fix",2),Cmd("enqueue",6)>>
 [] OTHER -> <<Cmd("enqueue",0),Cmd("enqueue",1),Cmd("enqueue",3),Cmd("enqueue",4),Cmd("enqueue",5)>>
Target == IF Scene="chain" THEN 4 ELSE 3
Disjoint == IF Scene="chain" THEN 5 ELSE 4
VARIABLES state,cursor,outputs,finished
vars == <<state,cursor,outputs,finished>>
Init == /\ state=K!Initial /\ cursor=0 /\ outputs= <<>> /\ finished={}
Record == /\ cursor<Len(Commands)
 /\ LET c==Commands[cursor+1] tr==K!Fold(Policy,W,state,c.kind,c.tx)
    IN /\ state'=tr.next /\ outputs'=outputs \o tr.outputs
 /\ cursor'=cursor+1 /\ UNCHANGED finished
Fix(t) == /\ cursor=Len(Commands) /\ Scene \in {"bridge","chain"}
 /\ t \in state.held \ {0}
 /\ LET tr==K!Fold(Policy,W,state,"fix",t) IN
       /\ state'=tr.next /\ outputs'=outputs \o tr.outputs
 /\ finished'=finished \cup {t} /\ UNCHANGED cursor
Quiet == cursor=Len(Commands) /\ (Scene \notin {"bridge","chain"} \/ state.held={0})
Next == Record \/ (\E t \in 0..6:Fix(t)) \/ (Quiet /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Record) /\ (\A t \in 0..6:WF_vars(Fix(t)))
Safety == K!Sound(W,state)
Closed == K!Eligible(Policy,W,state)={}
Independent == <> (Disjoint \in finished)
Bypass == <> (Target \in finished)
DirectHeld == Scene \in {"bridge","chain"} => 0 \in state.held \/ cursor=0
NoDirectBypass == Scene \in {"bridge","chain"} => (IF Scene="chain" THEN 6 ELSE 5) \notin state.held \cup finished
YoungerBoundary == (Scene="younger" /\ cursor=Len(Commands) /\ Policy \in {"drain","head"}) =>
 /\ 2 \in state.held /\ 1 \in K!Waiting(state) /\ 3 \in K!Waiting(state) /\ 4 \in state.held
CancelRemoves == (Scene="cancel" /\ cursor=Len(Commands)) =>
 /\ 1 \notin K!Items(state.order) /\ 0 \notin K!Items(state.order)
 /\ 6 \in state.held
NoBypass == Target \notin finished
NoYoungerBoundary == ~(Scene="younger" /\ cursor=Len(Commands) /\
 2 \in state.held /\ 1 \in K!Waiting(state) /\ 3 \in K!Waiting(state))
NoCancellationCompletion == ~(Scene="cancel" /\ cursor=Len(Commands) /\ 6 \in state.held)
=============================================================================
