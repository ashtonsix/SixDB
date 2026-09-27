------------------------- MODULE AdmissionInertNetwork -------------------------
EXTENDS Contracts, Integers
CONSTANTS Streams, Entries, Holders, Generations, Losses, Reset, Bug, Service, Atomic
ASSUME Service="folded"
A == INSTANCE AdmissionKernel
J == INSTANCE DurableLog
C == INSTANCE CertificateInterface
Q == INSTANCE AdmissionInertSends
P == [streams |-> Streams, entries |-> Entries, holders |-> Holders,
      generations |-> Generations, losses |-> Losses, reset |-> Reset,
      bad |-> Bug, atomic |-> Atomic, owner |-> "source", actor |-> "producer"]
JP == [owners |-> {P.owner}, actors |-> {P.actor},
       subscribers |-> [o \in {P.owner} |-> {P.actor}],
       initialConfig |-> [o \in {P.owner} |-> 1]]
VARIABLES state, journal, network, refines, evidence
vars == <<state,journal,network,refines,evidence>>
Init == /\ state=A!Init(P) /\ journal=J!Init(JP) /\ network={} /\ refines=C!Initial(P,C!Project(state)) /\ evidence=TRUE
KernelStep == \E t \in Q!Actions(P,state):
  /\ state'=t.next /\ evidence'=(evidence /\ t.valid) /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(t.next))) /\ UNCHANGED journal /\ network'=Q!Network(P,state',network \cup Elements(t.emissions))
JournalStep == /\ Service="full"
               /\ \E t \in J!Actions(JP,journal):
                    /\ journal'=t.next /\ UNCHANGED <<state,refines,evidence>>
                    /\ network'=Q!Network(P,state',network \cup Elements(t.emissions))
InputStep == \E e \in network:
  \/ /\ Service="full" /\ e.kind="journal.submit"
     /\ \E t \in J!Receive(JP,journal,e):
       /\ journal'=t.next /\ UNCHANGED <<state,refines,evidence>> /\ network'=Q!Network(P,state',network \ {e})
  \/ /\ Service="folded" /\ e.kind="journal.submit"
     /\ state'=Q!State(P,A!Apply(P,state,e.body)) /\ UNCHANGED evidence
    /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(state'))) /\ UNCHANGED journal /\ network'=Q!Network(P,state',network \ {e})
  \/ /\ e.kind # "journal.submit" /\ \E t \in Q!Receive(P,state,e):
       /\ state'=t.next /\ evidence'=(evidence /\ t.valid) /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(t.next))) /\ UNCHANGED journal
       /\ network'=Q!Network(P,state',(network \ {e}) \cup Elements(t.emissions))
Done == /\ state.promised=A!Keys(P)
        /\ state.generation=Generations
        /\ \A k \in A!Keys(P):Generations \in state.certified[k]
Terminal == Done /\ UNCHANGED vars
Next == KernelStep \/ JournalStep \/ InputStep \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(KernelStep) /\ WF_vars(JournalStep) /\ WF_vars(InputStep)
RefinesCertificateService == refines
CertificateEvidence == evidence
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
=============================================================================
