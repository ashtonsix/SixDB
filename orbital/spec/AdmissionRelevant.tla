------------------------- MODULE AdmissionRelevant -------------------------
EXTENDS Contracts, Integers
A == INSTANCE AdmissionKernel
B == INSTANCE AdmissionCapability
C == INSTANCE CertificateInterface

\* Diagnostic candidate: discard only state no future transition can use.
\* Atomic persisted packages make <= generation inbox items inert; domain
\* destruction also clears the inbox, so it cannot reactivate a discarded item.
\* A certificate is issued once per key/generation. After issuance its receipt
\* proof is already checked and no future correct certification reads it.
NeededEvidence(p,s,r) ==
 p.bad="stale-renewal" \/
 ~(\E c \in s.certificates:c.key=r.key /\ c.generation=r.generation)
State(p,s) ==
 LET b == B!State(p,s)
 IN [b EXCEPT
   !.inbox=[h \in A!Holders(p) |-> IF p.bad="forget-inbox" THEN {} ELSE
     {x \in b.inbox[h]:~(p.atomic /\ A!HasCopy(b,h,x.key) /\
                          b.disk[h][x.key].generation>=x.generation)}],
   !.receipts=IF p.bad="forget-current-evidence" THEN {} ELSE {r \in b.receipts:NeededEvidence(p,b,r)},
   !.issued={r \in b.issued:NeededEvidence(p,b,r)}]
NormalizeEvent(e) == B!NormalizeEvent(e)
Emissions(es) == B!Emissions(es)
Evidence(p,s,es) == B!Evidence(p,s,es)
NormalizeTransition(p,s,t) ==
 [tag |-> t.tag,next |-> State(p,t.next),emissions |-> Emissions(t.emissions),
  valid |-> Evidence(p,s,t.emissions)]
Actions(p,s) == {NormalizeTransition(p,s,t):t \in A!Actions(p,s)}
Receive(p,s,e) == {NormalizeTransition(p,s,t):t \in A!Receive(p,s,e)}
ActionCorrespondence(p,s) == Actions(p,s)=Actions(p,State(p,s))
InputCorrespondence(p,s,es) == \A e \in es:
 IF e.kind="journal.submit"
 THEN State(p,A!Apply(p,s,e.body))=State(p,A!Apply(p,State(p,s),NormalizeEvent(e).body))
 ELSE Receive(p,s,e)=Receive(p,State(p,s),NormalizeEvent(e))

\* Discard only deliveries whose effect is permanently inert. A payload can
\* refill an inbox after loss, so it stays queued until that destination cannot
\* lose its store again within this explicitly bounded fault model.
Dead(p,s,e) ==
 CASE e.kind="admission.payload" ->
   (s.lost>=p.losses \/ e.dst \in s.lostDomains) /\ p.atomic /\
   A!HasCopy(s,e.dst,e.body.key) /\ s.disk[e.dst][e.body.key].generation>=e.body.generation
 [] e.kind="admission.receipt" ->
   ~NeededEvidence(p,s,e.body) \/ e.body \in s.receipts
 [] e.kind="admission.discovered" ->
   e.body.key \in s.discovered /\ (~NeededEvidence(p,s,e.body) \/ e.body \in s.receipts)
 [] e.kind="admission.authorize" -> e.body.generation \in s.authorized[e.dst]
 [] e.kind="admission.need" -> s.peerStorage[e.dst][e.src]>=e.body.storage
 [] e.kind="admission.certificate" -> e.body.generation \in s.certified[e.body.key]
 [] e.kind="journal.submit" -> e.body.id \in s.seen
 [] OTHER -> FALSE
Network(p,s,es) == {NormalizeEvent(e):e \in {x \in es:~Dead(p,s,x)}}
Inert(p,s,e) ==
 IF e.kind="journal.submit" THEN State(p,A!Apply(p,s,e.body))=State(p,s)
 ELSE \A t \in A!Receive(p,s,e):State(p,t.next)=State(p,s) /\ t.emissions= <<>>
InputsInert(p,s,es) == \A e \in es:Dead(p,s,e) => Inert(p,s,e)
Successors(p,s,es) == {t.next:t \in A!Actions(p,s)} \cup
 UNION {IF e.kind="journal.submit" THEN {A!Apply(p,s,e.body)}
        ELSE {t.next:t \in A!Receive(p,s,e)}:e \in es}
DeadStable(p,s,es) == \A e \in {x \in es:Dead(p,s,x)}:
 \A n \in Successors(p,s,es):Dead(p,n,e)
=============================================================================
