------------------------ MODULE DeliveryDataflow ------------------------
EXTENDS Contracts, Integers
CONSTANTS Contributions, Recipients, Origins, Routes, Duplicates, Transform,
          Dynamic, Reset, Cancel, Join, Bug, Service
D == INSTANCE DeliveryKernel
J == INSTANCE DurableLog
S == INSTANCE JournalSchedule
Base == 1..Contributions
P == [keys |-> Base \cup (IF Dynamic THEN {Contributions+1} ELSE {}),baseKeys |-> Base,
 child |-> Contributions+1,parent |-> 1,origins |-> 1..Origins,
 recipients |-> {ToString(r):r \in 1..Recipients},initialMembers |->
   {ToString(r):r \in (IF Join THEN 1..(Recipients-1) ELSE 1..Recipients)},
 inputs |-> [o \in 1..Origins |-> [k \in Base |-> k+2]],
 lineage |-> "lineage-a",call |-> "call",cut |-> 4,interpretation |-> "program-v1",
 forms |-> IF Transform THEN {"raw","folded"} ELSE {"raw"},
 routes |-> Routes,duplicates |-> Duplicates,dynamic |-> Dynamic,
 cancelRecipients |-> {"1"},reset |-> Reset,cancel |-> Cancel,join |-> Join,bad |-> Bug,owner |-> "producer"]
Owners == {P.owner} \cup P.recipients
JP == [owners |-> Owners,actors |-> Owners,
 subscribers |-> [o \in Owners |-> {o}],initialConfig |-> [o \in Owners |-> 1]]
Authored == Service \in {"scheduled","scheduled-folded"}
Full == Service \in {"full","scheduled"}
VARIABLE run
vars == <<run>>
state == run.state
journal == run.journal
network == run.network
Init == run=[state |-> D!Init(P),journal |-> J!Init(JP),network |-> {}]
CommandKind(tr) == IF tr.emissions# <<>> THEN
 IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE "" ELSE ""
ResetRecipient == "1"
TransformedRecipient == ToString(IF Join THEN Recipients-1 ELSE Recipients)
Allowed(tr) == IF ~Authored THEN TRUE ELSE
 /\ (tr.tag#"route-replacement" \/ state.transport#{})
 /\ (tr.tag#"recipient-process-reset" \/
       (tr.next.restarted \ state.restarted={ResetRecipient} /\
        DOMAIN state.custody[ResetRecipient]#{} /\ ResetRecipient \notin state.done[1]))
 /\ (CommandKind(tr)#"delivery.complete" \/ ~Reset \/
       Head(tr.emissions).body.owner#ResetRecipient \/ ResetRecipient \in state.restarted)
 /\ (CommandKind(tr)#"delivery.ack" \/ state.route=Routes)
 /\ (CommandKind(tr)#"delivery.expand" \/
       state.outbox[P.parent].coverage \subseteq state.done[P.parent] \cup state.cancelled)
 /\ (CommandKind(tr)#"delivery.join" \/ Base \subseteq state.closed)
 /\ (CommandKind(tr)#"delivery.cancel" \/
       DOMAIN state.custody[ResetRecipient]#{})
KernelSteps == {Transition(tr.tag,[run EXCEPT !.state=tr.next,
 !.network=@ \cup Elements(tr.emissions)],<<>>):tr \in {x \in D!Actions(P,state):Allowed(x)}}
JournalSteps == IF ~Full THEN {} ELSE
 {Transition(tr.tag,[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)],<<>>):tr \in J!Actions(JP,journal)}
InputAllowed(e) == IF ~Authored \/ ~Transform THEN TRUE
 ELSE IF e.kind="delivery.payload" /\ e.dst=TransformedRecipient /\ e.body.form="raw"
      THEN D!Logical(P,e.body.key) \in DOMAIN state.custody[e.dst] ELSE TRUE
Input(e) ==
 IF ~InputAllowed(e) THEN {}
 ELSE IF Full /\ e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input." \o e.kind,[run EXCEPT !.journal=tr.next,
        !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in J!Receive(JP,journal,e)}
 ELSE IF ~Full /\ e.kind="journal.submit"
 THEN {Transition("input.fold",[run EXCEPT !.state=D!Apply(P,state,e.body),!.network=@ \ {e}],<<>>)}
 ELSE {Transition("input." \o e.kind,[run EXCEPT !.state=tr.next,
        !.network=(@ \ {e}) \cup Elements(tr.emissions)],<<>>):tr \in D!Receive(P,state,e)}
Inputs == UNION {Input(e):e \in network}
IsFault(tr) ==
 IF tr.tag \in {"recipient-process-reset","route-replacement"} THEN TRUE
 ELSE \E e \in tr.next.network \ network:
   IF e.kind="journal.submit" THEN e.body.kind \in {"delivery.join","delivery.cancel"} ELSE FALSE
LocalFault(tr) == tr.tag \in {"recipient-process-reset","route-replacement"} \/
 CommandKind(tr) \in {"delivery.join","delivery.cancel"}
LocalChoices == {tr \in D!Actions(P,state):Allowed(tr)}
EligibleInputs == {e \in network:Input(e)#{}}
Step ==
 IF EligibleInputs#{} THEN
   LET e==CHOOSE e \in EligibleInputs:TRUE
       tr==CHOOSE tr \in Input(e):TRUE IN run'=tr.next
 ELSE IF Full /\ J!Actions(JP,journal)#{} THEN
   LET tr==CHOOSE tr \in J!Actions(JP,journal):TRUE
   IN run'=[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)]
 ELSE LET normal=={tr \in LocalChoices:~LocalFault(tr)} IN
   /\ normal#{}
   /\ LET closing=={tr \in normal:tr.tag="discharge-output-obligation"}
          tr==CHOOSE tr \in (IF closing#{} THEN closing ELSE normal):TRUE
      IN run'=[run EXCEPT !.state=tr.next,!.network=@ \cup Elements(tr.emissions)]
Fault == \E tr \in {x \in LocalChoices:LocalFault(x)}:
 run'=[run EXCEPT !.state=tr.next,!.network=@ \cup Elements(tr.emissions)]
\* The unrestricted leaf enumerates local transitions directly. Building a set
\* of whole product successor states merely to enumerate it is much slower in
\* TLC; this branch has exactly the original unconstrained transition choices.
DirectKernel == \E tr \in D!Actions(P,state):
 run'=[run EXCEPT !.state=tr.next,!.network=@ \cup Elements(tr.emissions)]
DirectJournal ==
 /\ Full
 /\ \E tr \in J!Actions(JP,journal):
      run'=[run EXCEPT !.journal=tr.next,!.network=@ \cup Elements(tr.emissions)]
DirectInput == \E e \in network: \E tr \in Input(e):run'=tr.next
Done == /\ P.baseKeys \subseteq state.closed
        /\ (P.dynamic => (P.child \in state.closed \/ P.initialMembers \subseteq state.cancelled))
        /\ (~Join \/ P.recipients \ P.initialMembers \subseteq state.joinedComplete)
        /\ (~Authored \/ (state.route=Routes /\ (~Reset \/ ResetRecipient \in state.restarted)))
Terminal == Done /\ UNCHANGED vars
Next == (IF Authored THEN Step \/ Fault ELSE DirectKernel \/ DirectJournal \/ DirectInput) \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ IF Authored THEN WF_vars(Step) /\ WF_vars(Fault)
                   ELSE WF_vars(DirectKernel) /\ WF_vars(DirectJournal) /\ WF_vars(DirectInput)
EffectMultiplicity == D!EffectMultiplicity(P,state)
DerivedAgreement == D!DerivedAgreement(P,state)
Coverage == D!Coverage(P,state)
Canonical == D!Canonical(P,state)
Checkpoints == D!Checkpoints(P,state)
JoinedCoverage == D!JoinedCoverage(P,state)
CancellationAuthority == D!CancellationAuthority(P,state)
NoContentConflict == state.conflicts={}
ProcessedBeforeRelease == \A k \in state.closed: \A r \in state.outbox[k].coverage \ state.cancelled:
 D!Logical(P,k) \in DOMAIN state.applied[r]
Completes == <>Done
NoComplete == ~Done
NoReroutedCompletion == ~(Done /\ \E a,b \in state.arrivals:a.id=b.id /\ a.recipient=b.recipient /\ a.route=1 /\ b.route=2)
NoRestartCompletion == ~(Done /\ \E c \in state.restartCuts: \E replay \in state.replays:
 c.recipient=replay.recipient /\ c.custody#{} /\ \E command \in Elements(replay.prefix):command.kind="delivery.custody")
NoTransformedUse == ~(\E c \in state.custodyEvidence:c.carriers#{} /\ \A a \in c.carriers:a.form="folded")
NoJoinedService == ~(Done /\ \E j \in state.joinHistory:Base \subseteq j.closed /\ j.recipient \in state.joinedComplete)
NoSharedCancel == ~(Done /\ "1" \in state.cancelled /\ "2" \notin state.cancelled /\
                    \A k \in Base:D!Logical(P,k) \in DOMAIN state.applied["2"])
=============================================================================
