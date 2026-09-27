------------------------- MODULE AdmissionInertSends -------------------------
EXTENDS Contracts, Integers
A == INSTANCE AdmissionKernel
Q == INSTANCE AdmissionRelevant
\* Experimental additional quotient. No production send guard reads these facts.
\* Only send identifiers whose resulting delivery is permanently inert may go.
InertIDs(p,s) ==
 UNION {{ToString(<<"authorize",g,h>>):g \in s.authorized[h]}:h \in A!Holders(p)} \cup
 UNION {{ToString(<<"resume-copies",d,h,s.storage[d]>>):
   h \in {x \in A!Holders(p):s.peerStorage[x][d]>=s.storage[d]}}:d \in A!Holders(p)} \cup
 UNION {{ToString(<<"receipt",h,k,s.disk[h][k].generation,s.disk[h][k].storage>>):
   k \in {x \in A!Keys(p):~Q!NeededEvidence(p,s,[key|->x,generation|->s.disk[h][x].generation])}}:
   h \in A!Holders(p)} \cup
 UNION {{ToString(<<h,d,k,g,s.storage[h],s.peerStorage[h][d]>>):
   g \in {x \in 1..p.generations:
     (s.lost>=p.losses \/ d \in s.lostDomains) /\ p.atomic /\
     A!HasCopy(s,d,k) /\ s.disk[d][k].generation>=x}}:
   h \in A!Holders(p),d \in A!Holders(p),k \in A!Keys(p)}
State(p,s) == LET b==Q!State(p,s)
 IN [b EXCEPT !.packets=@ \ InertIDs(p,b)]
NormalizeEvent(e) == Q!NormalizeEvent(e)
Emissions(es) == Q!Emissions(es)
Evidence(p,s,es) == Q!Evidence(p,s,es)
Network(p,s,es) == Q!Network(p,s,es)
NormalizeTransition(p,s,t) ==
 [tag|->t.tag,next|->State(p,t.next),emissions|->Emissions(t.emissions),
  valid|->Evidence(p,s,t.emissions)]
Actions(p,s) == {NormalizeTransition(p,s,t):t \in A!Actions(p,s)}
Receive(p,s,e) == {NormalizeTransition(p,s,t):t \in A!Receive(p,s,e)}
\* Match complete non-stuttering successors separately by fair action family.
\* Guard envelope equality by its typed ID; comparing unlike records through
\* TLC's record-set ordering can otherwise compare numeric/string endpoints.
SameNetwork(es,fs) == Cardinality(es)=Cardinality(fs) /\
 \A e \in es: \E f \in fs: IF e.id=f.id THEN e=f ELSE FALSE
Observation(p,s,es,valid) ==
 [state|->State(p,s),network|->Network(p,s,es),evidence|->valid]
Same(x,y) == IF x.state=y.state /\ x.evidence=y.evidence
             THEN SameNetwork(x.network,y.network) ELSE FALSE
KObservation(p,s,es,t) ==
 Observation(p,t.next,es \cup Elements(t.emissions),Evidence(p,s,t.emissions))
KernelCovered(p,s,es,n,ns) == \A t \in A!Actions(p,s):
 LET next==KObservation(p,s,es,t)
 IN Same(next,Observation(p,s,es,TRUE)) \/
    (\E u \in A!Actions(p,n):
      IF t.tag=u.tag THEN Same(next,KObservation(p,n,ns,u)) ELSE FALSE)
Inputs(p,s,e) == IF e.kind="journal.submit"
 THEN {Transition("fold",A!Apply(p,s,e.body),<<>>)} ELSE A!Receive(p,s,e)
IObservation(p,s,es,e,t) ==
 Observation(p,t.next,(es \ {e}) \cup Elements(t.emissions),Evidence(p,s,t.emissions))
InputsCovered(p,s,es,n,ns) == \A e \in es: \A t \in Inputs(p,s,e):
 LET next==IObservation(p,s,es,e,t)
 IN Same(next,Observation(p,s,es,TRUE)) \/
    (\E f \in ns: IF e.kind=f.kind /\ e.id=f.id
      THEN \E u \in Inputs(p,n,f):
        IF t.tag=u.tag THEN Same(next,IObservation(p,n,ns,f,u)) ELSE FALSE
      ELSE FALSE)
Correspondence(p,s,es) ==
 LET n==State(p,s) net==Network(p,s,es)
 IN /\ KernelCovered(p,s,es,n,net) /\ KernelCovered(p,n,net,s,es)
    /\ InputsCovered(p,s,es,n,net) /\ InputsCovered(p,n,net,s,es)
=============================================================================
