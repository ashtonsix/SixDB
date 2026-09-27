------------------------- MODULE AdmissionCapability -------------------------
EXTENDS Contracts, Integers
A == INSTANCE AdmissionKernel
Q == INSTANCE AdmissionQuotient
C == INSTANCE CertificateInterface
\* Certificate proof is verified when actually emitted. Subsequent protocol
\* actions read only its immutable key/generation; receipt evidence stays exact.
State(p,s) == [Q!State(p,s) EXCEPT !.certificates={C!Certificate(c):c \in @}]
NormalizeEvent(e) == IF e.kind="admission.certificate" THEN [e EXCEPT !.body=C!Certificate(@)] ELSE e
Emissions(es) == [i \in 1..Len(es) |-> NormalizeEvent(es[i])]
Evidence(p,s,es) == C!Emissions(p,s,es) /\ \A e \in Elements(es):
 e.kind="admission.certificate" => e.body.proof \subseteq s.issued
NormalizeTransition(p,s,t) == [tag |-> t.tag,next |-> State(p,t.next),
 emissions |-> Emissions(t.emissions),valid |-> Evidence(p,s,t.emissions)]
Actions(p,s) == {NormalizeTransition(p,s,t):t \in A!Actions(p,s)}
Receive(p,s,e) == {NormalizeTransition(p,s,t):t \in A!Receive(p,s,e)}
ActionCorrespondence(p,s) == Actions(p,s)=Actions(p,State(p,s))
InputCorrespondence(p,s,es) == \A e \in es:
 IF e.kind="journal.submit"
 THEN State(p,A!Apply(p,s,e.body))=State(p,A!Apply(p,State(p,s),NormalizeEvent(e).body))
 ELSE Receive(p,s,e)=Receive(p,State(p,s),NormalizeEvent(e))
=============================================================================
