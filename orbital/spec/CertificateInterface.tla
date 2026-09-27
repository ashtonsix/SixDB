------------------------- MODULE CertificateInterface -------------------------
EXTENDS Contracts, Integers
A == INSTANCE AdmissionKernel
\* This is the actual abstract provider explored by AdmissionFrontiers. The
\* consumer never inspects a certificate's holder/storage proof: it receives only
\* (key,generation). Physical proof truth is checked separately at emission.
Certificate(c) == [key |-> c.key,generation |-> c.generation]
Project(s) == [registered |-> s.registered,generation |-> s.generation,
 begins |-> s.begins,lost |-> s.lost,lostDomains |-> s.lostDomains,
 issued |-> {Certificate(c):c \in s.certificates},received |-> s.certified,
 frontier |-> s.frontier,promised |-> s.promised,submitted |-> s.submitted,seen |-> s.seen]
Initial(p,o) == o=Project(A!Init(p))
RegisterCommand(p) == Command(p.owner,<<"source-register">>,"admission.register",[streams |-> 1..p.streams,holders |-> A!Holders(p)])
BeginCommand(p,g) == Command(p.owner,<<"protect",g>>,"admission.protect",[generation |-> g])
FrontierCommand(p,st,n,g) == Command(p.owner,<<"frontier",st,n,g>>,"admission.frontier",[stream |-> st,lsn |-> n,generation |-> g])
Commands(p) == {RegisterCommand(p)} \cup {BeginCommand(p,g):g \in 1..p.generations} \cup
 {FrontierCommand(p,st,n,g):st \in 1..p.streams,n \in 1..p.entries,g \in 1..p.generations}
Submit(p,o,c) ==
 IF c.id \in o.submitted THEN {} ELSE
 IF CASE c.kind="admission.register" -> ~o.registered
 [] c.kind="admission.protect" -> o.registered /\ c.body.generation=o.generation+1 /\
       (o.generation=0 \/ \A st \in 1..p.streams:o.frontier[st]=p.entries)
 [] OTHER -> c.body.generation=o.generation /\ o.generation>0 /\ c.body.lsn>o.frontier[c.body.stream] /\
    (IF p.bad="frontier-hole" THEN o.generation \in o.received[<<c.body.stream,c.body.lsn>>]
     ELSE \A i \in (o.frontier[c.body.stream]+1)..c.body.lsn:o.generation \in o.received[<<c.body.stream,i>>])
 THEN {[o EXCEPT !.submitted=@ \cup {c.id}]} ELSE {}
Apply(p,o,c) ==
 IF c.id \in o.seen THEN o ELSE
 LET n == [o EXCEPT !.seen=@ \cup {c.id}]
 IN CASE c.kind="admission.register" -> [n EXCEPT !.registered=TRUE]
 [] c.kind="admission.protect" -> IF c.body.generation=o.generation+1
    THEN [n EXCEPT !.generation=c.body.generation,!.begins[c.body.generation]=o.lost] ELSE n
 [] OTHER -> [n EXCEPT !.frontier[c.body.stream]=IF @>=c.body.lsn THEN @ ELSE c.body.lsn,
                       !.promised=@ \cup {<<c.body.stream,k>>:k \in 1..c.body.lsn}]
Protocol(p,o) == UNION {Submit(p,o,c):c \in Commands(p)} \cup
 {Apply(p,o,c):c \in {cmd \in Commands(p):cmd.id \in o.submitted \ o.seen}}
Issue(p,o,k) == IF o.generation>0
 THEN {[o EXCEPT !.issued=@ \cup {[key |-> k,generation |-> o.generation]}]} ELSE {}
Receipt(p,o,c) == [o EXCEPT !.received[c.key]=@ \cup {c.generation}]
Loss(p,o) == IF o.generation>0 /\ o.lost<p.losses
 THEN {[o EXCEPT !.lost=@+1,!.lostDomains=@ \cup {h}]:h \in A!Holders(p) \ o.lostDomains} ELSE {}
NextStates(p,o) == Protocol(p,o) \cup Loss(p,o) \cup
 UNION {Issue(p,o,k):k \in A!Keys(p)} \cup {Receipt(p,o,c):c \in o.issued}
Step(p,o,n) == n=o \/ n \in NextStates(p,o)
Emissions(p,s,es) == \A e \in Elements(es):
 IF e.kind="admission.certificate" THEN A!CertificateValid(e.body) /\ e.body.proof \subseteq s.receipts
 ELSE IF e.kind \in {"admission.receipt","admission.discovered"}
 THEN LET cp == s.disk[e.src][e.body.key]
      IN /\ e.src \in A!Holders(p) /\ e.body.holder=e.src
         /\ cp.body /\ cp.decoder /\ cp.generation=e.body.generation
         /\ cp.storage=e.body.storage /\ cp.bytes=e.body.bytes
 ELSE IF e.kind="journal.submit" /\ e.body.kind="admission.frontier"
 THEN \A i \in (s.frontier[e.body.body.stream]+1)..e.body.body.lsn:
      e.body.body.generation \in s.certified[<<e.body.body.stream,i>>]
 ELSE TRUE
=============================================================================
