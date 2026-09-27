-------------------------- MODULE AdmissionKernel --------------------------
EXTENDS Integers, Sequences, FiniteSets, TLC, Contracts

\* Producer payloads are not journal command bodies. This service creates
\* receipts only from physical body+decoder records, and submits frontier
\* commands separately. Fault counters are observer state, never admission input.
Keys(p) == {<<s,n>> : s \in 1..p.streams, n \in 1..p.entries}
Holders(p) == 1..p.holders
Package(k,g) == [key |-> k, generation |-> g,
                 bytes |-> <<k[1], k[2], 17>>, decoder |-> "tuple-v1"]
EmptyCopy == [body |-> FALSE, decoder |-> FALSE, generation |-> 0,
              bytes |-> <<>>, interpretation |-> "", storage |-> 0]
Init(p) ==
  [registered |-> FALSE, generation |-> 0, submitted |-> {},
   inbox |-> [h \in Holders(p) |-> {}],
   authorized |-> [h \in Holders(p) |-> {}],
   peerStorage |-> [h \in Holders(p) |-> [d \in Holders(p) |-> 1]],
   disk |-> [h \in Holders(p) |-> [k \in Keys(p) |-> EmptyCopy]],
   storage |-> [h \in Holders(p) |-> 1],
   pending |-> [h \in Holders(p) |-> {}],
   receipts |-> {}, issued |-> {}, certificates |-> {}, certified |-> [k \in Keys(p) |-> {}],
   frontier |-> [s \in 1..p.streams |-> 0],
   packets |-> {}, sent |-> {}, seen |-> {}, cursor |-> 0,
   lost |-> 0, lostDomains |-> {}, begins |-> [g \in 1..p.generations |-> 0],
   promised |-> {}, discovered |-> {}, producerUp |-> TRUE,
   producerReset |-> FALSE, unknownAtReset |-> {}, scanned |-> [h \in Holders(p) |-> {}], scan |-> [h \in Holders(p) |-> 0],
   lossAfterAdmission |-> FALSE, gap |-> FALSE]

Emit(p,id,src,dst,kind,body) == Event(id,src,dst,kind,body)
Submit(p,s,id,kind,body) ==
  LET c == Command(p.owner,id,kind,body)
  IN Transition("admission-propose",
       [s EXCEPT !.submitted = @ \cup {c.id}],
       <<Emit(p,<<"submit",c.id>>,p.actor,p.owner,"journal.submit",c)>>)

Apply(p,s,c) ==
  IF c.id \in s.seen THEN s
  ELSE LET n == [s EXCEPT !.seen = @ \cup {c.id}]
       IN CASE c.kind = "admission.register" ->
                 [n EXCEPT !.registered = TRUE]
            [] c.kind = "admission.protect" ->
                 IF c.body.generation = s.generation + 1
                 THEN [n EXCEPT !.generation = c.body.generation,
                        !.begins[c.body.generation] = s.lost]
                 ELSE n
            [] c.kind = "admission.frontier" ->
                 [n EXCEPT !.frontier[c.body.stream] =
                    IF @ >= c.body.lsn THEN @ ELSE c.body.lsn,
                    !.promised = @ \cup {<<c.body.stream,k>> : k \in 1..c.body.lsn}]
            [] OTHER -> n

Receive(p,s,e) ==
  IF e.kind = "journal.deliver"
  THEN IF e.body.owner = p.owner /\ e.body.index = s.cursor + 1
       THEN {Transition("admission-journal-delivery",
               [Apply(p,s,e.body.command) EXCEPT !.cursor = e.body.index], <<>>)}
       ELSE {}
  ELSE IF e.kind = "admission.authorize"
  THEN {Transition("holder-protection-request",
          [s EXCEPT !.authorized[e.dst]=@ \cup {e.body.generation}],<<>>)}
  ELSE IF e.kind = "admission.need"
  THEN {Transition("receive-copy-resumption-request",
       [s EXCEPT !.peerStorage[e.dst][e.src]=IF @>=e.body.storage THEN @ ELSE e.body.storage],<<>>)}
  ELSE IF e.kind = "admission.payload" /\ e.dst \in Holders(p)
  THEN {Transition("payload-arrival",
          [s EXCEPT !.inbox[e.dst] = @ \cup {e.body}], <<>>)}
  ELSE IF e.kind = "admission.certificate"
  THEN {Transition("receive-package-certificate",
          [s EXCEPT !.certified[e.body.key]=@ \cup {e.body.generation}],<<>>)}
  ELSE IF e.kind = "admission.discovered"
  THEN {Transition("recovery-learns-tail-entry",
          [s EXCEPT !.discovered=@ \cup {e.body.key},
                    !.receipts=@ \cup {e.body}],<<>>)}
  ELSE IF e.kind = "admission.receipt"
  THEN {Transition("receipt-arrival",
          [s EXCEPT !.receipts = @ \cup {e.body}], <<>>)}
  ELSE {}

Register(p,s) ==
  IF ~s.registered /\ ToString(<<"source-register">>) \notin s.submitted
  THEN {Submit(p,s,<<"source-register">>,"admission.register",
               [streams |-> 1..p.streams, holders |-> Holders(p)])}
  ELSE {}

Begin(p,s) ==
  LET g == s.generation + 1
  IN IF s.registered /\ g <= p.generations /\
        ToString(<<"protect",g>>) \notin s.submitted /\
        (g = 1 \/ \A st \in 1..p.streams : s.frontier[st] = p.entries)
     THEN {Submit(p,s,<<"protect",g>>,"admission.protect",[generation |-> g])}
     ELSE {}

Authorize(p,s,h) ==
 LET id == <<"authorize",s.generation,h>>
 IN IF s.generation>0 /\ ToString(id) \notin s.packets
    THEN {Transition("send-protection-request",[s EXCEPT !.packets=@ \cup {ToString(id)}],
          <<Emit(p,id,p.actor,h,"admission.authorize",[generation |-> s.generation])>>)} ELSE {}

HasCopy(s,h,k) == s.disk[h][k].body /\ s.disk[h][k].decoder

\* Initial producer write and repair both enter through actual immutable
\* packages. A repair source must possess complete bytes in its current store.
Produce(p,s,k) ==
  IF s.registered /\ s.generation > 0 /\ s.producerUp /\
     <<k,s.storage[1]>> \notin s.sent
  THEN LET b == Package(k,s.generation)
       IN {Transition("producer-submission",
             [s EXCEPT !.sent = @ \cup {<<k,s.storage[1]>>},
                       !.inbox[1] = @ \cup {b}], <<>>)}
  ELSE {}

Forward(p,s,h,k,d,g) ==
  IF g \in s.authorized[h] /\ (\A newer \in s.authorized[h]:newer<=g) /\ HasCopy(s,h,k) /\
     (h=1 \/ p.generations>1 \/ p.reset) /\
     (h # d \/ s.disk[h][k].generation < g) /\
     ToString(<<h,d,k,g,s.storage[h],s.peerStorage[h][d]>>) \notin s.packets
  THEN LET b == [Package(k,g) EXCEPT !.bytes = s.disk[h][k].bytes,
                  !.decoder = s.disk[h][k].interpretation]
       IN {Transition("payload-forward",
             [s EXCEPT !.packets = @ \cup {ToString(<<h,d,k,g,s.storage[h],s.peerStorage[h][d]>>)}],
             <<Emit(p,<<"body",h,d,k,g,s.storage[h],s.peerStorage[h][d]>>,h,d,
                    "admission.payload",b)>>)}
  ELSE {}

\* A recovered storage endpoint asks registered peers to resume copies. Sources
\* learn the destination incarnation through this message, never by reading
\* another node's true storage counter. Immutable payloads remain idempotent.
RequestCopies(p,s,d,h) ==
 LET id == <<"resume-copies",d,h,s.storage[d]>>
 IN IF s.registered /\ s.generation>0 /\ s.storage[d]>1 /\ d#h /\
       p.bad#"no-copy-resumption" /\ ToString(id) \notin s.packets
    THEN {Transition("request-copy-resumption",[s EXCEPT !.packets=@ \cup {ToString(id)}],
         <<Emit(p,id,d,h,"admission.need",[storage |-> s.storage[d]])>>)} ELSE {}

Write(p,s,h,b,part) ==
  LET k == b.key
      old == s.disk[h][k]
      id == <<k,b.generation,part,s.storage[h]>>
  IN IF b.generation \in s.authorized[h] /\ id \notin s.pending[h] /\
        (IF part = "body" THEN ~old.body ELSE ~old.decoder)
     THEN {Transition("payload-write-submit",
             [s EXCEPT !.pending[h] = @ \cup {id}], <<>>)}
     ELSE {}

CompleteWrite(p,s,h,b,part) ==
  LET k == b.key
      id == <<k,b.generation,part,s.storage[h]>>
      old == s.disk[h][k]
      base == old
      value == IF part = "body"
               THEN [base EXCEPT !.body = TRUE, !.bytes = b.bytes]
               ELSE [base EXCEPT !.decoder = TRUE, !.interpretation = b.decoder]
      updated == [value EXCEPT !.storage = s.storage[h]]
  IN IF id \in s.pending[h]
     THEN {Transition("payload-write-persist",
             [s EXCEPT !.pending[h] = @ \ {id}, !.disk[h][k] = updated], <<>>)}
     ELSE {}

\* Atomic package publication includes the generation tag with body+decoder.
\* The split-write family below retains the generation-zero discovery interval.
\* Renew a generation only after a request received in that generation and
\* complete local persistence. Payloads are immutable across renewals; renewing
\* metadata cannot overwrite a still-protected old copy with an incomplete one.
Whole(p,s,h,b) ==
  IF p.atomic /\ b.generation \in s.authorized[h] /\ ~HasCopy(s,h,b.key)
  THEN {Transition("persist-immutable-package",
          [s EXCEPT !.disk[h][b.key]=[body |-> TRUE,decoder |-> TRUE,generation |-> b.generation,
             bytes |-> b.bytes,interpretation |-> b.decoder,storage |-> s.storage[h]]],<<>>)}
  ELSE {}

Renew(p,s,h,b) ==
  IF b.generation \in s.authorized[h] /\ s.disk[h][b.key].generation < b.generation /\
     s.disk[h][b.key].body /\
     (s.disk[h][b.key].decoder \/ p.bad = "missing-decoder")
  THEN {Transition("persist-protection-generation",
          [s EXCEPT !.disk[h][b.key].generation = b.generation], <<>>)}
  ELSE {}

Receipt(p,s,h,k) ==
  LET cp == s.disk[h][k]
      b == [key |-> k, holder |-> IF p.bad="receipt-alias" THEN h+1 ELSE h, generation |-> cp.generation,
            storage |-> cp.storage, bytes |-> cp.bytes]
      id == <<"receipt",h,k,cp.generation,cp.storage>>
  IN IF cp.body /\ (cp.decoder \/ p.bad = "missing-decoder") /\
        cp.generation > 0 /\ ToString(id) \notin s.packets
     THEN {Transition("durability-receipt",
             [s EXCEPT !.packets = @ \cup {ToString(id)},!.issued=@ \cup {b}],
             <<Emit(p,id,h,p.actor,"admission.receipt",b)>>)}
     ELSE {}

Eligible(p,s,k) ==
  LET rs == {r \in s.receipts : r.key = k /\
           (r.generation = s.generation \/ p.bad = "stale-renewal")}
  IN Cardinality({r.holder : r \in rs}) >= (IF p.bad = "one-copy" THEN 1 ELSE 2)

CertificateValid(c) ==
 /\ Cardinality({r.holder:r \in c.proof})>=2
 /\ \A r \in c.proof:r.key=c.key /\ r.generation=c.generation /\ r.bytes=Package(c.key,1).bytes
Certify(p,s,k) ==
  IF s.generation > 0 /\ (s.producerUp \/ k \in s.discovered) /\ Eligible(p,s,k) /\
     ~(\E c \in s.certificates:c.key=k /\ c.generation=s.generation)
  THEN LET c == [key |-> k,generation |-> s.generation,
       proof |-> {r \in s.receipts:r.key=k /\
          (r.generation=s.generation \/ p.bad="stale-renewal")}]
       IN {Transition("issue-package-certificate",
          [s EXCEPT !.certificates=@ \cup {c}],
          <<Emit(p,<<"certificate",k,s.generation>>,p.actor,p.actor,"admission.certificate",c)>>)}
  ELSE {}


Advance(p,s,st,n) ==
  IF n > s.frontier[st] /\ s.generation > 0 /\
     ToString(<<"frontier",st,n,s.generation>>) \notin s.submitted /\
     (IF p.bad = "frontier-hole"
      THEN s.generation \in s.certified[<<st,n>>]
      ELSE \A i \in (s.frontier[st]+1)..n : s.generation \in s.certified[<<st,i>>])
  THEN {Submit(p,s,<<"frontier",st,n,s.generation>>,"admission.frontier",
               [stream |-> st, lsn |-> n, generation |-> s.generation])}
  ELSE {}

Lose(p,s,h) ==
  IF s.lost < p.losses /\ h \notin s.lostDomains /\ s.generation > 0
  THEN {Transition("destroy-storage-domain",
          [s EXCEPT !.disk[h] = [k \in Keys(p) |-> EmptyCopy],
                    !.storage[h] = @+1, !.inbox[h] = {}, !.pending[h] = {},
                    !.lost = @+1, !.lostDomains = @ \cup {h},
                    !.lossAfterAdmission = @ \/
                      (\E k \in s.promised : HasCopy(s,h,k))], <<>>)}
  ELSE {}

ProducerReset(p,s) ==
  IF p.reset /\ ~s.producerReset /\ s.registered /\ s.generation > 0 /\
     (\E h \in Holders(p),k \in Keys(p): HasCopy(s,h,k))
  THEN {Transition("producer-disappears",
          [s EXCEPT !.producerUp = FALSE, !.producerReset = TRUE,
           !.unknownAtReset={k \in Keys(p):k \notin s.promised /\
             \E h \in Holders(p):HasCopy(s,h,k)}], <<>>)}
  ELSE {}

\* Discovery enumerates an actual holder namespace registered before the producer
\* died. Returned keys are learned, not injected as the caller's recovery list.
Scan(p,s,h) ==
  IF ~s.producerUp /\ s.registered /\ s.scan[h] < p.streams*p.entries
  THEN LET i == s.scan[h]+1
           k == <<((i-1) \div p.entries)+1, ((i-1) % p.entries)+1>>
           cp == s.disk[h][k]
           body == [key |-> k,holder |-> h,generation |-> cp.generation,
                    storage |-> cp.storage,bytes |-> cp.bytes]
       IN {Transition("discover-tail-entry",
             [s EXCEPT !.scan[h] = i,
              !.issued=IF HasCopy(s,h,k) THEN @ \cup {body} ELSE @,
              !.scanned[h]=IF HasCopy(s,h,k) THEN @ \cup {<<k,cp.storage,cp.generation>>} ELSE @],
             IF HasCopy(s,h,k)
             THEN <<Emit(p,<<"discover",h,k>>,h,p.actor,"admission.discovered",body)>>
             ELSE <<>>)}
  ELSE {}

\* A completed directory pass is not a permanent absence assertion. A later
\* completed/renewed local package keeps the subscription pending for another
\* pass. The holder inspects only its own namespace; the driver learns by reply.
ScanAgain(p,s,h) ==
 IF ~s.producerUp /\ s.scan[h]=p.streams*p.entries /\
    (\E k \in Keys(p):HasCopy(s,h,k) /\
      <<k,s.disk[h][k].storage,s.disk[h][k].generation>> \notin s.scanned[h])
 THEN {Transition("rescan-changed-tail",[s EXCEPT !.scan[h]=0],<<>>)} ELSE {}

Actions(p,s) ==
  Register(p,s) \cup Begin(p,s) \cup ProducerReset(p,s)
  \cup UNION {Produce(p,s,k) \cup Certify(p,s,k) : k \in Keys(p)}
  \cup UNION {Forward(p,s,h,k,d,g) : h \in Holders(p), k \in Keys(p), d \in Holders(p),g \in 1..p.generations}
  \cup UNION {RequestCopies(p,s,d,h):d \in Holders(p),h \in Holders(p)}
  \cup UNION {Receipt(p,s,h,k) : h \in Holders(p), k \in Keys(p)}
  \cup UNION {UNION {Renew(p,s,h,b) \cup Whole(p,s,h,b) \cup
       IF p.atomic THEN {} ELSE UNION {Write(p,s,h,b,a) \cup CompleteWrite(p,s,h,b,a) : a \in {"body","decoder"}} :
                 b \in s.inbox[h]} : h \in Holders(p)}
  \cup UNION {Advance(p,s,st,n) : st \in 1..p.streams, n \in 1..p.entries}
  \cup UNION {Authorize(p,s,h) \cup Lose(p,s,h) \cup Scan(p,s,h) \cup ScanAgain(p,s,h) : h \in Holders(p)}

\* Independent observations. No action calls these predicates.
CertificateEvidence(p,s) == \A c \in s.certificates:
  CertificateValid(c) /\ c.proof \subseteq s.issued
Contiguous(p,s) ==
  \A st \in 1..p.streams : \A n \in 1..s.frontier[st] : s.certified[<<st,n>>] # {}
ExactCopies(p,s) ==
  \A h \in Holders(p), k \in Keys(p) : HasCopy(s,h,k) =>
    /\ s.disk[h][k].bytes = Package(k,1).bytes
    /\ s.disk[h][k].interpretation = "tuple-v1"
Protection(p,s) ==
  \A k \in Keys(p) : \A g \in s.certified[k] :
    s.lost - s.begins[g] <= 1 => \E h \in Holders(p) : HasCopy(s,h,k)

=============================================================================
