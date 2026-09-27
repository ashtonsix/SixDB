-------------------------- MODULE AdmissionQuotient --------------------------
EXTENDS Contracts, Integers
A == INSTANCE AdmissionKernel
\* Only permanently obsolete send suppression flags are forgotten. Emitted
\* messages, receipt/certificate evidence, loss intervals and storage are unchanged.
LiveFlags(p,s) ==
 {ToString(<<"authorize",s.generation,h>>):h \in A!Holders(p)} \cup
 {ToString(<<"resume-copies",d,h,s.storage[d]>>):d \in A!Holders(p),h \in A!Holders(p)} \cup
 {ToString(<<"receipt",h,k,s.disk[h][k].generation,s.disk[h][k].storage>>):
   h \in A!Holders(p),k \in A!Keys(p)} \cup
 UNION {{ToString(<<h,d,k,g,s.storage[h],s.peerStorage[h][d]>>):
   d \in A!Holders(p),k \in A!Keys(p),
   g \in {x \in s.authorized[h]:\A y \in s.authorized[h]:y<=x}}:h \in A!Holders(p)}
State(p,s) == [s EXCEPT !.packets=IF p.bad="forget-live-send" THEN {} ELSE @ \cap LiveFlags(p,s),
 !.sent=@ \cap {<<k,s.storage[1]>>:k \in A!Keys(p)}]
NormalizeTransition(p,t) == [t EXCEPT !.next=State(p,@)]
Actions(p,s) == {NormalizeTransition(p,t):t \in A!Actions(p,s)}
Receive(p,s,e) == {NormalizeTransition(p,t):t \in A!Receive(p,s,e)}
\* Exact enabled action/emission equality, stronger than merely belonging to a
\* permissive common model. The complete fine family checks these at each state.
ActionCorrespondence(p,s) == Actions(p,s)=Actions(p,State(p,s))
InputCorrespondence(p,s,es) == \A e \in es:
 IF e.kind="journal.submit"
 THEN State(p,A!Apply(p,s,e.body))=State(p,A!Apply(p,State(p,s),e.body))
 ELSE Receive(p,s,e)=Receive(p,State(p,s),e)
=============================================================================
