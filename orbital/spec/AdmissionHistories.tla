------------------------- MODULE AdmissionHistories -------------------------
EXTENDS Contracts, Integers
CONSTANTS Shape, Bug
A == INSTANCE AdmissionKernel
C == INSTANCE CertificateInterface
P == [streams |-> IF Shape="independent" THEN 2 ELSE 1,
 entries |-> IF Shape="independent" THEN 1 ELSE IF Shape \in {"tail","resumption"} THEN 1 ELSE 2,
 holders |-> IF Shape="resumption" THEN 2 ELSE 3,generations |-> IF Shape="repair" THEN 2 ELSE 1,
 losses |-> IF Shape="repair" THEN 2 ELSE 1,reset |-> Shape="tail",atomic |-> TRUE,
 bad |-> Bug,owner |-> "source",actor |-> "producer"]
First == IF Shape="independent" THEN <<2,1>> ELSE <<1,P.entries>>
Last == <<1,1>>
VARIABLES state,network,phase,refines,earlyCertificate,repairLoss,tailFound,pulse
vars == <<state,network,phase,refines,earlyCertificate,repairLoss,tailFound,pulse>>
Init == /\ state=A!Init(P) /\ network={} /\ phase=0
 /\ refines=C!Initial(P,C!Project(state)) /\ earlyCertificate=FALSE /\ repairLoss=FALSE /\ tailFound=FALSE /\ pulse=0
WorkKey == IF phase<=1 \/ phase=5 THEN First ELSE Last
MoveSet ==
 CASE phase=0 -> A!Register(P,state) \cup A!Begin(P,state)
 [] OTHER ->
 A!Register(P,state) \cup A!Begin(P,state) \cup
 UNION {A!Authorize(P,state,h):h \in A!Holders(P)} \cup
 UNION {A!RequestCopies(P,state,d,h):d \in A!Holders(P),h \in A!Holders(P)} \cup
 A!Produce(P,state,WorkKey) \cup
 UNION {A!Forward(P,state,h,WorkKey,d,g):h \in (IF state.generation=2 THEN {3} ELSE {1}),d \in A!Holders(P),g \in 1..P.generations} \cup
 UNION {A!Receipt(P,state,h,WorkKey):h \in A!Holders(P)} \cup
 UNION {UNION {A!Whole(P,state,h,b) \cup A!Renew(P,state,h,b):b \in state.inbox[h]}:h \in A!Holders(P)} \cup
 (IF Shape="tail" /\ ~state.producerReset THEN {} ELSE A!Certify(P,state,WorkKey)) \cup
 UNION {A!Advance(P,state,st,n):st \in 1..P.streams,n \in 1..P.entries} \cup
 UNION {A!Scan(P,state,h) \cup A!ScanAgain(P,state,h):h \in A!Holders(P)}
\* The scripted seams choose the earliest enabled service stage. They retain
\* all choices inside that stage, including which durable holder answers first.
Rank(tag) == CASE tag="admission-propose" -> 0
 [] tag="send-protection-request" -> 1 [] tag="producer-submission" -> 2
 [] tag="persist-immutable-package" -> 3 [] tag="persist-protection-generation" -> 4
 [] tag="payload-forward" -> 5 [] tag="durability-receipt" -> 6
 [] tag="discover-tail-entry" -> 7 [] OTHER -> 8
Selected == {t \in MoveSet:~(\E u \in MoveSet:Rank(u.tag)<Rank(t.tag))}
Move == \E t \in Selected:
 /\ state'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(t.next)) /\ C!Emissions(P,state,t.emissions))
 /\ UNCHANGED <<phase,earlyCertificate,repairLoss,tailFound>>
Deliver == \E e \in network:
 /\ state'=(IF e.kind="journal.submit" THEN A!Apply(P,state,e.body)
            ELSE (CHOOSE t \in A!Receive(P,state,e):TRUE).next)
 /\ network'=network \ {e}
 /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(state')))
 /\ tailFound'=(tailFound \/ (e.kind="admission.discovered" /\ e.body.key \in state.unknownAtReset))
 /\ UNCHANGED <<phase,earlyCertificate,repairLoss>>
AdvancePhase ==
 \/ /\ phase=0 /\ state.generation=1 /\ phase'=1
    /\ UNCHANGED <<state,network,refines,earlyCertificate,repairLoss,tailFound>>
 \/ /\ phase=1 /\ state.certified[First]#{} /\ phase'=2
    /\ earlyCertificate'=(First#Last /\ state.certified[Last]={})
    /\ UNCHANGED <<state,network,refines,repairLoss,tailFound>>
 \/ /\ Shape="repair" /\ phase=2 /\ state.promised=A!Keys(P) /\ state.generation=1
    /\ \E t \in A!Lose(P,state,1):state'=t.next
    /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(state')))
    /\ phase'=3 /\ UNCHANGED <<network,earlyCertificate,repairLoss,tailFound>>
 \/ /\ Shape="repair" /\ phase=3 /\ state.generation=2
    /\ \E t \in A!Lose(P,state,2):state'=t.next
    /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(state')))
    /\ phase'=4 /\ repairLoss'=TRUE /\ UNCHANGED <<network,earlyCertificate,tailFound>>
 \/ /\ Shape="repair" /\ phase=4 /\ 2 \in state.certified[Last] /\ phase'=5
    /\ UNCHANGED <<state,network,refines,earlyCertificate,repairLoss,tailFound>>
TailReset == /\ Shape="tail" /\ ~state.producerReset
 /\ \E h \in A!Holders(P):state.disk[h][First].generation=1
 /\ \E t \in A!ProducerReset(P,state):state'=t.next
 /\ UNCHANGED <<network,phase,refines,earlyCertificate,repairLoss,tailFound>>
ResumeLoss == /\ Shape="resumption" /\ ~repairLoss /\ state.inbox[2]#{} /\ ~A!HasCopy(state,2,First)
 /\ \E t \in A!Lose(P,state,2):state'=t.next
 /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(state')))
 /\ repairLoss'=TRUE /\ UNCHANGED <<network,phase,earlyCertificate,tailFound>>
Done == /\ state.promised=A!Keys(P)
 /\ state.generation=P.generations
 /\ \A k \in A!Keys(P):P.generations \in state.certified[k]
 /\ (Shape \notin {"repair","resumption"} \/ repairLoss) /\ (Shape#"tail" \/ tailFound)
ProtocolNext ==
 /\ (IF ENABLED ResumeLoss THEN ResumeLoss
     ELSE IF ENABLED TailReset THEN TailReset
     ELSE IF ENABLED AdvancePhase THEN AdvancePhase
     ELSE IF network#{} THEN Deliver
     ELSE Move \/ (Done /\ UNCHANGED vars))
 /\ UNCHANGED pulse
\* Independent heartbeat service continues even if the copy protocol stalls.
Heartbeat == pulse'=1-pulse /\ UNCHANGED <<state,network,phase,refines,earlyCertificate,repairLoss,tailFound>>
Next == ProtocolNext \/ Heartbeat
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(ProtocolNext)
RefinesCertificateService == refines
CertificateEvidence == A!CertificateEvidence(P,state)
Contiguous == A!Contiguous(P,state)
Protection == A!Protection(P,state)
ExactCopies == A!ExactCopies(P,state)
Completes == <>Done
NoSeam == ~(Done /\ IF Shape \in {"repair","resumption"} THEN repairLoss ELSE IF Shape="tail" THEN tailFound ELSE earlyCertificate)
=============================================================================
