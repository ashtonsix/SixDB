-------------------- MODULE AdmissionCapabilityObserverProbe --------------------
EXTENDS Contracts, Integers
CONSTANTS Bug, BypassValidation
A == INSTANCE AdmissionKernel
C == INSTANCE CertificateInterface
Q == INSTANCE AdmissionCapability

\* A small observer audit, not a replacement for physical or liveness families.
\* Explore malformed certificates too: the two observers must agree on rejection.
P == [streams |-> 1,entries |-> 1,holders |-> 2,generations |-> 1,
 losses |-> 0,reset |-> FALSE,atomic |-> TRUE,bad |-> Bug,
 owner |-> "source",actor |-> "producer"]
VARIABLES state,network,legacyRefines,compactRefines,evidence,announced
vars == <<state,network,legacyRefines,compactRefines,evidence,announced>>
Init == /\ state=A!Init(P) /\ network={}
 /\ legacyRefines=TRUE /\ compactRefines=TRUE /\ evidence=TRUE /\ announced={}

Take(t,remaining) ==
 LET step == C!Step(P,C!Project(state),C!Project(t.next))
     valid == Q!Evidence(P,Q!State(P,state),t.emissions)
 IN /\ state'=t.next
    /\ network'=remaining \cup Elements(t.emissions)
    /\ legacyRefines'=(legacyRefines /\ step /\ C!Emissions(P,state,t.emissions))
    /\ compactRefines'=(compactRefines /\ step)
    /\ evidence'=(evidence /\ (BypassValidation \/ valid))
    /\ announced'=announced \cup
         {e.body:e \in {x \in Elements(t.emissions):x.kind="admission.certificate"}}
KernelStep == \E t \in A!Actions(P,state):Take(t,network)
InputStep == \E e \in network:
 IF e.kind="journal.submit"
 THEN Take(Transition("folded-command",A!Apply(P,state,e.body),<<>>),network \ {e})
 ELSE \E t \in A!Receive(P,state,e):Take(t,network \ {e})
Done == state.promised=A!Keys(P)
Next == KernelStep \/ InputStep \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars

\* The old conjunction includes pre-normalization physical emission checks.
\* The new wrapper splits the same obligations between refinement and evidence.
ObserverAgreement ==
 (legacyRefines /\ A!CertificateEvidence(P,state))=(compactRefines /\ evidence)
ProofOrigin == state.certificates \subseteq announced
NetworkIdentity == Cardinality(network)=Cardinality({Q!NormalizeEvent(e):e \in network})
ExactRepresentation == Q!ActionCorrespondence(P,state) /\ Q!InputCorrespondence(P,state,network)
=============================================================================
