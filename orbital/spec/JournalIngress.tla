----------------------------- MODULE JournalIngress -----------------------------
EXTENDS Contracts
CONSTANT JournalNone
J == INSTANCE JournalKernel
D == INSTANCE DurableLog
P == [owners|->{"owner"},actors|->{"reader"},subscribers|->[o \in {"owner"}|->{"reader"}],
 initialConfig|->[o \in {"owner"}|->0],configs|->{0},members|->[c \in {0}|->{"A","B","C"}],
 owner|->[c \in {0}|->"owner"],ballots|->{1},leaders|->[c \in {0}|->[b \in {1}|->"A"]],
 successors|->[c \in {0}|->{}],ingress|->[o \in {"owner"}|->{"A","B","C"}],
 mode|->"correct",compaction|->FALSE,opaqueCertificates|->TRUE,abstractVoters|->{},atomicVoters|->{}]
CommandValue == Command("owner","frontier-1","admission.frontier",[stream|->1,lsn|->1])
Offer(n) == Event(<<"envelope",n>>,<<"producer",n>>,"owner","journal.submit",CommandValue)
VARIABLES state,offered,projection
vars == <<state,offered,projection>>
Init == /\ state=J!Init(P) /\ offered={} /\ projection=TRUE
\* Two genuine submission envelopes carry the identical immutable command to
\* different followers. Forwarding/learning/append remain the actual J actions.
Input(n) ==
 /\ n \notin offered /\ (n=1 \/ 1 \in offered)
 /\ LET params==[P EXCEPT !.ingress["owner"]={IF n=1 THEN "B" ELSE "C"}]
    IN \E tr \in J!Receive(params,state,Offer(n)):
       /\ state'=tr.next /\ offered'=offered \cup {n}
 /\ UNCHANGED projection
Protocol == \E tr \in J!Actions(P,state):
 /\ tr.next#state /\ state'=tr.next
 /\ projection'=(projection /\ D!PrefixExtension(J!ChosenLog(P,state),J!ChosenLog(P,tr.next)) /\
    \A e \in Elements(tr.emissions):D!DeliverySound(J!ChosenLog(P,tr.next),e))
 /\ UNCHANGED offered
Delivered == \E e \in state.outputHistory:e.kind="journal.deliver" /\ e.body.command=CommandValue
Done == offered={1,2} /\ Delivered
Next == (\E n \in {1,2}:Input(n)) \/ Protocol \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
PrefixAgreement == \A x,y \in state.chosenHistory:Comparable(x.seq,y.seq)
LearnedEvidence == \A v \in P.members[0]:
 LET q==state.learned[0][v] IN Len(q.seq)=0 \/ J!Quorum(P,state.acceptedHistory,q.cfg,q.ballot,q.seq)
UniqueCommand == \A h \in state.chosenHistory:
 Cardinality({i \in 1..Len(h.seq):h.seq[i].id=CommandValue.id})<=1
SubmittedBytes == \A e \in state.submitted:e \in {Offer(1),Offer(2)} /\ e.body=CommandValue
Refinement == projection
ProviderRefinement == D!Contract(P,J!ChosenLog(P,state),state.proposed,state.outputHistory)
NoDuplicateIngress == ~(Done /\ state.submitted={Offer(1),Offer(2)} /\
                       CommandValue \in state.requests[0]["B"] /\ CommandValue \in state.requests[0]["C"])
=============================================================================
