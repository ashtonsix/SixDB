------------------------- MODULE AdmissionUnsafeNetworkProbe -------------------------
EXTENDS Contracts, Integers
CONSTANTS Streams, Entries, Holders, Generations, Losses, Reset, Bug, Service, Atomic
A == INSTANCE AdmissionKernel
J == INSTANCE DurableLog
C == INSTANCE CertificateInterface
Q == INSTANCE AdmissionRelevant
P == [streams |-> Streams, entries |-> Entries, holders |-> Holders,
      generations |-> Generations, losses |-> Losses, reset |-> Reset,
      bad |-> Bug, atomic |-> Atomic, owner |-> "source", actor |-> "producer"]
JP == [owners |-> {P.owner}, actors |-> {P.actor},
       subscribers |-> [o \in {P.owner} |-> {P.actor}],
       initialConfig |-> [o \in {P.owner} |-> 1]]
VARIABLES state, journal, network, refines
vars == <<state,journal,network,refines>>
Init == /\ state=A!Init(P) /\ journal=J!Init(JP) /\ network={} /\ refines=C!Initial(P,C!Project(state))
KernelStep == \E t \in A!Actions(P,state):
  /\ state'=t.next /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(t.next)) /\ C!Emissions(P,state,t.emissions)) /\ UNCHANGED journal /\ network'=network \cup Elements(t.emissions)
JournalStep == /\ Service="full"
               /\ \E t \in J!Actions(JP,journal):
                    /\ journal'=t.next /\ UNCHANGED <<state,refines>>
                    /\ network'=network \cup Elements(t.emissions)
InputStep == \E e \in network:
  \/ /\ Service="full" /\ e.kind="journal.submit"
     /\ \E t \in J!Receive(JP,journal,e):
       /\ journal'=t.next /\ UNCHANGED <<state,refines>> /\ network'=network \ {e}
  \/ /\ Service="folded" /\ e.kind="journal.submit"
     /\ state'=A!Apply(P,state,e.body)
    /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(state'))) /\ UNCHANGED journal /\ network'=network \ {e}
  \/ /\ e.kind # "journal.submit" /\ \E t \in A!Receive(P,state,e):
       /\ state'=t.next /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(t.next))) /\ UNCHANGED journal
       /\ network'=(network \ {e}) \cup Elements(t.emissions)
Done == /\ state.promised=A!Keys(P)
        /\ state.generation=Generations
        /\ \A k \in A!Keys(P):Generations \in state.certified[k]
Terminal == Done /\ UNCHANGED vars
Next == KernelStep \/ JournalStep \/ InputStep \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(KernelStep) /\ WF_vars(JournalStep) /\ WF_vars(InputStep)
RefinesCertificateService == refines
ExactRepresentation == Q!ActionCorrespondence(P,state) /\ Q!InputCorrespondence(P,state,network)
InertDeliveries == Q!InputsInert(P,state,network)
PermanentlyInert == Q!DeadStable(P,state,network)
CertificateEvidence == A!CertificateEvidence(P,state)
Contiguous == A!Contiguous(P,state)
ExactCopies == A!ExactCopies(P,state)
Protection == A!Protection(P,state)
Completes == <>Done
Discoverable == Reset /\ state.producerReset =>
  \A k \in state.promised: \E h \in A!Holders(P):A!HasCopy(state,h,k)
NoAdmission == state.promised={}
NoHole == ~(Entries>1 /\ state.certified[<<1,Entries>>]#{} /\ state.certified[<<1,1>>]={})
NoRecoveredTail == ~(state.unknownAtReset \cap state.discovered \cap state.promised # {})
NoRepair == ~(state.generation=2 /\ state.begins[2]>0 /\ 2 \in state.certified[<<1,1>>])
UnsafeDead(s,e) ==
 IF e.kind="admission.payload"
 THEN Atomic /\ A!HasCopy(s,e.dst,e.body.key) /\ s.disk[e.dst][e.body.key].generation>=e.body.generation
 ELSE Q!Dead(P,s,e)
UnsafeStable == \A e \in {x \in network:UnsafeDead(state,x)}:
 \A n \in Q!Successors(P,state,network):UnsafeDead(n,e)
=============================================================================
