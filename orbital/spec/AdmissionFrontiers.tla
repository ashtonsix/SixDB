------------------------- MODULE AdmissionFrontiers -------------------------
EXTENDS Contracts, Integers
CONSTANTS Streams, Entries, Generations, Losses, Bug
A == INSTANCE AdmissionKernel
C == INSTANCE CertificateInterface
P == [streams |-> Streams,entries |-> Entries,generations |-> Generations,
 holders |-> 3,losses |-> Losses,reset |-> FALSE,atomic |-> TRUE,bad |-> Bug,
 owner |-> "source",actor |-> "producer"]
VARIABLE state
vars == <<state>>
Init == state=C!Project(A!Init(P))
Protocol == \E n \in C!Protocol(P,state):state'=n /\ state'#state
Issue(k) == \E n \in C!Issue(P,state,k):state'=n /\ state'#state
Receive(k) == \E c \in {cert \in state.issued:cert.key=k}:state'=C!Receipt(P,state,c) /\ state'#state
Loss == \E n \in C!Loss(P,state):state'=n
Done == /\ state.generation=Generations
 /\ \A st \in 1..Streams:state.frontier[st]=Entries
 /\ \A k \in A!Keys(P):Generations \in state.received[k]
Next == Protocol \/ Loss \/ (\E k \in A!Keys(P):Issue(k) \/ Receive(k)) \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
\* Conditional service: stable complete source packages and fair certificate
\* production/transport. Concrete mapping establishes safety trace inclusion;
\* it does not transfer these availability assumptions through destruction.
FairSpec == Spec /\ WF_vars(Protocol) /\ \A k \in A!Keys(P):WF_vars(Issue(k)) /\ WF_vars(Receive(k))
Contiguous == \A st \in 1..Streams: \A i \in 1..state.frontier[st]:state.received[<<st,i>>]#{}
InterfaceRefinement == state \in [registered:BOOLEAN,generation:0..Generations,
 begins:[1..Generations -> 0..Losses],lost:0..Losses,lostDomains:SUBSET A!Holders(P),
 issued:SUBSET [key:A!Keys(P),generation:1..Generations],received:[A!Keys(P) -> SUBSET (1..Generations)],
 frontier:[1..Streams -> 0..Entries],promised:SUBSET A!Keys(P),submitted:SUBSET {c.id:c \in C!Commands(P)},seen:SUBSET {c.id:c \in C!Commands(P)}]
Completes == <>Done
NoIndependentStream == ~(Streams>1 /\ state.frontier[1]=Entries /\ state.frontier[2]=0)
NoOutOfOrderCertificate == ~(Entries>1 /\ state.received[<<1,Entries>>]#{} /\ state.received[<<1,1>>]={})
NoTwoGenerations == state.generation<2
=============================================================================
