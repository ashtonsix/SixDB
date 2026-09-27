----------------------------- MODULE AdmissionCold -----------------------------
EXTENDS Contracts, Integers
CONSTANT Bug
A == INSTANCE AdmissionKernel
J == INSTANCE DurableLog
C == INSTANCE CertificateInterface
P == [streams|->1,entries|->1,holders|->2,generations|->1,losses|->1,
 reset|->FALSE,atomic|->TRUE,bad|->"none",owner|->"source",actor|->"producer"]
JP == [owners|->{P.owner},actors|->{P.actor,"cold-holder"},
 subscribers|->[o \in {P.owner}|->{P.actor}],initialConfig|->[o \in {P.owner}|->1]]
Key == <<1,1>>
Query == Event("reinstall-authorization","cold-holder",P.owner,"journal.recover",[reason|->"cold-restart"])
VARIABLES state,journal,network,cold,requested,recovered,proof,pulse,refines
vars == <<state,journal,network,cold,requested,recovered,proof,pulse,refines>>
Init == /\ state=A!Init(P) /\ journal=J!Init(JP) /\ network={}
 /\ cold=FALSE /\ requested=FALSE /\ recovered=FALSE /\ proof=[prefix|-><<>>,sound|->TRUE] /\ pulse=0 /\ refines=TRUE
\* Barrier command IDs belong to the journal service, not the admission fold's
\* logical deduplication interface. Keep their real cursor/seen state locally.
Project(s) == C!Project([s EXCEPT !.seen=@ \cap {c.id:c \in C!Commands(P)}])
ColdReady == ~cold /\ 1 \in state.authorized[2] /\ A!HasCopy(state,1,Key)
ColdRestart == /\ ColdReady
 /\ \E t \in A!Lose(P,state,2):
    state'=[t.next EXCEPT !.authorized[2]={},!.peerStorage[2]=[h \in A!Holders(P)|->1]]
 /\ cold'=TRUE
 /\ refines'=(refines /\ C!Step(P,Project(state),Project(state')))
 /\ UNCHANGED <<journal,network,requested,recovered,proof,pulse>>
Request == /\ cold /\ ~requested /\ network'=network \cup {Query} /\ requested'=TRUE
 /\ UNCHANGED <<state,journal,cold,recovered,proof,pulse,refines>>
Recover(e) ==
 /\ e.kind="journal.snapshot" /\ e.dst="cold-holder" /\ cold /\ ~recovered
 /\ e.body.owner=P.owner /\ e.body.recovery=Query.id
 /\ \E c \in Elements(e.body.prefix):c.kind="admission.register" /\ 2 \in c.body.holders
 /\ LET generations=={c.body.generation:c \in {x \in Elements(e.body.prefix):x.kind="admission.protect"}}
    IN network'=(network \ {e}) \cup
       (IF Bug="no-authorization-recovery" THEN {} ELSE
        {Event(<<"recovered-authorization",e.id,g>>,e.dst,2,"admission.authorize",[generation|->g]):g \in generations})
 /\ recovered'=TRUE /\ proof'=[prefix|->e.body.prefix,sound|->J!DeliverySound(journal.log,e)]
 /\ UNCHANGED <<state,journal,cold,requested,pulse,refines>>
Inputs(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.journal",[a|->state,j|->t.next,n|->(network \ {e}) \cup Elements(t.emissions),ok|->refines],<<>>):t \in J!Receive(JP,journal,e)}
 ELSE {Transition("input.admission",[a|->t.next,j|->journal,n|->(network \ {e}) \cup Elements(t.emissions),
       ok|->refines /\ C!Step(P,Project(state),Project(t.next))],<<>>):t \in A!Receive(P,state,e)}
PendingInputs == UNION {Inputs(e):e \in {x \in network:x.kind#"journal.snapshot"}}
Input ==
 /\ PendingInputs#{}
 /\ LET t==CHOOSE x \in PendingInputs:TRUE
    IN /\ state'=t.next.a /\ journal'=t.next.j /\ network'=t.next.n /\ refines'=t.next.ok
 /\ UNCHANGED <<cold,requested,recovered,proof,pulse>>
Journal ==
 /\ J!Actions(JP,journal)#{}
 /\ LET t==CHOOSE x \in J!Actions(JP,journal):TRUE
    IN /\ journal'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ UNCHANGED <<state,cold,requested,recovered,proof,pulse,refines>>
OrdinaryActions == {t \in A!Actions(P,state):t.tag#"destroy-storage-domain"}
Ordinary ==
 /\ OrdinaryActions#{}
 /\ LET t==CHOOSE x \in OrdinaryActions:TRUE
    IN /\ state'=t.next /\ network'=network \cup Elements(t.emissions)
       /\ refines'=(refines /\ C!Step(P,Project(state),Project(t.next)) /\ C!Emissions(P,state,t.emissions))
 /\ UNCHANGED <<journal,cold,requested,recovered,proof,pulse>>
Service == IF ColdReady THEN ColdRestart ELSE IF ENABLED Request THEN Request
 ELSE IF \E e \in network:e.kind="journal.snapshot" THEN \E e \in network:Recover(e)
 ELSE IF ENABLED Input THEN Input ELSE IF ENABLED Journal THEN Journal ELSE Ordinary
Heartbeat == pulse'=1-pulse /\ UNCHANGED <<state,journal,network,cold,requested,recovered,proof,refines>>
Done == cold /\ recovered /\ Key \in state.promised /\ A!HasCopy(state,2,Key) /\
 \E r \in state.receipts:r.holder=2 /\ r.storage=state.storage[2] /\ r.generation=1
Next == Service \/ Heartbeat
Spec == Init /\ [][Next]_vars /\ WF_vars(Service)
Completes == <>Done
CertificateEvidence == A!CertificateEvidence(P,state)
Protection == A!Protection(P,state)
ExactCopies == A!ExactCopies(P,state)
Refines == refines
RecoveryEvidence == recovered => proof.sound /\ Prefix(proof.prefix,journal.log[P.owner]) /\
 \E c \in Elements(proof.prefix):c.kind="admission.protect" /\ c.body.generation=1
RecoveredMembership == recovered => \E c \in Elements(proof.prefix):
 c.kind="admission.register" /\ 2 \in c.body.holders
NoReinstalled == ~Done
=============================================================================
