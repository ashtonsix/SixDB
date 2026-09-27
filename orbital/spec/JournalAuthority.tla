---------------------------- MODULE JournalAuthority ---------------------------
EXTENDS Contracts
CONSTANTS JournalNone, Mode, NumBallots, NumCommands, Transfers, Topology, AllowReset,
          AllowDestruction, Compaction, RequireRecovery, ExplicitVoters, OpaqueCertificates, AbstractVoters, Ingress, SlowVoter
J == INSTANCE JournalKernel WITH JournalNone <- JournalNone
DL == INSTANCE DurableLog
OC == INSTANCE JournalOrderContract

Configs == 0..Transfers
Members(c) ==
    IF Topology = "overlap"
    THEN IF c = 0 THEN {"A", "B", "C"}
         ELSE IF c = 1 THEN {"A", "B", "D"} ELSE {"B", "D", "E"}
    ELSE IF c = 0 THEN {"A", "B", "C"}
         ELSE IF c = 1 THEN {"D", "E", "F"} ELSE {"G", "H", "I"}
Leaders(c) ==
    IF Topology = "overlap"
    THEN IF c = 0 THEN << "A", "B", "C" >>
         ELSE IF c = 1 THEN <<"A", "B", "D">> ELSE <<"B", "D", "E">>
    ELSE IF c = 0 THEN <<"A", "B", "C">>
         ELSE IF c = 1 THEN <<"D", "E", "F">> ELSE <<"G", "H", "I">>
P == [owners |-> {"owner"}, actors |-> {"reader"},
      subscribers |-> [j \in {"owner"} |-> {"reader"}],
      initialConfig |-> [j \in {"owner"} |-> 0],
      configs |-> Configs, members |-> [c \in Configs |-> Members(c)],
      owner |-> [c \in Configs |-> "owner"],
      leaders |-> [c \in Configs |-> [b \in 1..NumBallots |-> Leaders(c)[b]]],
      ballots |-> 1..NumBallots,
      successors |-> [c \in Configs |-> {t \in Configs : t > c}],
      mode |-> Mode, compaction |-> Compaction,
      ingress |-> [j \in {"owner"} |-> IF AllowDestruction THEN Members(0) ELSE {Ingress}], opaqueCertificates |-> OpaqueCertificates, abstractVoters |-> AbstractVoters,
      atomicVoters |-> (UNION {Members(c) : c \in Configs}) \ ExplicitVoters]
Commands == {Command("owner", i, "fixture.value", [value |-> i]) : i \in 1..NumCommands}
Offers == {Event(<<"offer", x.id>>, "producer", "owner", "journal.submit", x) :
             x \in Commands}
Handoffs == UNION {{Event(<<"handoff.request", c, t>>, "operator", "owner",
                   "journal.handoff.request", [cfg |-> c, target |-> t]) :
             t \in P.successors[c]} : c \in Configs}
RecoveryQuery == Event("recover-reader", "reader", "owner", "journal.recover", [reason |-> "restart"])
Inputs == Offers \cup Handoffs \cup (IF RequireRecovery THEN {RecoveryQuery} ELSE {})
VARIABLES state, offered, resetDone, destroyDone, projectionOK, orderOK, unrelated
InputEnabled(e) ==
    e.kind # "journal.recover" \/
      (state.acceptedHistory # {} /\ (~AllowReset \/ resetDone))
vars == <<state, offered, resetDone, destroyDone, projectionOK, orderOK, unrelated>>
Init == /\ state = J!Init(P)
        /\ offered = {}
        /\ resetDone = FALSE
        /\ destroyDone = FALSE
        /\ projectionOK = TRUE /\ orderOK = TRUE /\ unrelated = FALSE

Step(t) ==
    /\ t.next # state
    /\ state' = t.next
    /\ orderOK' = (orderOK /\ OC!Corresponds(P,state,t.next))
    /\ projectionOK' = (projectionOK /\
         DL!PrefixExtension(J!ChosenLog(P, state), J!ChosenLog(P, t.next)) /\
         \A e \in Elements(t.emissions) : DL!DeliverySound(J!ChosenLog(P, t.next), e))
    /\ UNCHANGED <<offered, resetDone, destroyDone, unrelated>>

InputStep ==
    \E e \in Inputs \ offered :
      /\ InputEnabled(e)
      /\ \E t \in J!Receive(P, state, e) :
        /\ state' = t.next
        /\ offered' = offered \cup {e}
        /\ UNCHANGED <<resetDone, destroyDone, projectionOK, orderOK, unrelated>>

RetryStep ==
    /\ AllowDestruction
    /\ \E e \in offered \cap Offers : \E t \in J!Receive(P,state,e) : Step(t)

ResetStep ==
    /\ AllowReset /\ ~resetDone
    /\ \E c \in {x \in Configs : "A" \in P.members[x]} : state.pending[c]["A"] # JournalNone
    /\ \E t \in J!Reset(P, state, "A") : state' = t.next
    /\ resetDone' = TRUE
    /\ UNCHANGED <<offered, destroyDone, projectionOK, orderOK, unrelated>>

DestroyStep ==
    /\ AllowDestruction /\ ~destroyDone
    /\ \E r \in state.acceptedHistory : r.voter = "A"
    /\ \E t \in J!Destroy(P, state, "A") : state' = t.next
    /\ destroyDone' = TRUE
    /\ UNCHANGED <<offered, resetDone, projectionOK, orderOK, unrelated>>

AllCommandsDelivered ==
    \A command \in Commands :
      \E e \in state.outputHistory :
        (e.kind = "journal.deliver" /\ e.body.command = command) \/
        (e.kind = "journal.snapshot" /\ command \in Elements(e.body.prefix))
WritesFinished ==
    /\ \A c \in Configs : \A v \in P.members[c] : state.pending[c][v] = JournalNone
    /\ state.callbacks = {}
RecoveryComplete == ~RequireRecovery \/
    \E e \in state.outputHistory : e.kind = "journal.snapshot"
Terminal == offered = Inputs /\ AllCommandsDelivered /\ WritesFinished /\ RecoveryComplete
Finish == /\ Terminal /\ UNCHANGED vars
ServiceAvailable(t) ==
    /\ \A c \in Configs : \A v \in P.members[c] :
         v = SlowVoter => t.next.disk[c][v] = state.disk[c][v] /\
             t.next.pending[c][v] = state.pending[c][v] /\
             t.next.learned[c][v] = state.learned[c][v]
    /\ Mode # "omit-quorum" \/
         \A c \in Configs : \A v \in P.members[c] \ {"A"} :
             t.next.disk[c][v].ab = state.disk[c][v].ab /\
             J!Full(t.next,c,v) = J!Full(state,c,v)
ProtocolStep == \E t \in J!Actions(P, state) : /\ ServiceAvailable(t) /\ Step(t)
OtherStep == /\ Mode = "omit-quorum" /\ unrelated' = ~unrelated
             /\ UNCHANGED <<state,offered,resetDone,destroyDone,projectionOK,orderOK>>
Next == InputStep \/ RetryStep \/ ProtocolStep \/ ResetStep \/ DestroyStep \/ OtherStep \/ Finish
Spec == Init /\ [][Next]_vars
LiveSpec == Spec /\ WF_vars(InputStep) /\ WF_vars(ProtocolStep) /\ WF_vars(OtherStep)

PrefixAgreement ==
    \A x, y \in state.chosenHistory : Comparable(x.seq, y.seq)
LearnedEvidence ==
    \A c \in Configs : \A v \in P.members[c] :
      LET cert == state.learned[c][v]
      IN Len(cert.seq) = 0 \/
           J!Quorum(P, state.acceptedHistory, cert.cfg, cert.ballot, cert.seq)
TerminalClosure ==
    \A r \in state.acceptedHistory : J!IsTerminalLast(r.cfg, r.seq)
SuccessorBinding ==
    \A x, y \in state.chosenHistory :
      \A i \in J!Stops(x.cfg, x.seq) :
        \A k \in J!Stops(y.cfg, y.seq) :
          x.cfg = y.cfg => x.seq[i].body.target = y.seq[k].body.target
InitializedFromHistory ==
    \A c \in Configs \ {0} : \A v \in P.members[c] :
      state.disk[c][v].initialized =>
        \E x \in state.chosenHistory :
          /\ J!Terminal(x.cfg, x.seq)
          /\ x.seq[Len(x.seq)].body.target = c
          /\ Prefix(x.seq, J!Full(state, c, v))
ChosenRetained ==
    \A q \in state.chosenHistory :
      \A r \in state.acceptedHistory :
        (r.cfg = q.cfg /\ r.voter \notin state.destroyed /\
         Prefix(q.seq, r.seq)) =>
           Prefix(q.seq, J!Full(state, r.cfg, r.voter))
PromiseDurable ==
    \A m \in state.net :
      (m.kind = "promise" /\ m.src \notin state.destroyed) =>
          state.disk[m.body.cfg][m.src].promise >= m.body.ballot
AcceptedCoherent ==
    \A r \in state.acceptedHistory :
      (r.voter \notin state.destroyed /\ state.disk[r.cfg][r.voter].ab = r.ballot) =>
         Prefix(r.seq, J!Full(state, r.cfg, r.voter))
MaterialPresent ==
    \A c \in Configs : \A v \in P.members[c] :
      J!BasePresent(state, state.disk[c][v])
DeliverySound ==
    \A e \in state.outputHistory : DL!DeliverySound(J!ChosenLog(P, state), e)
ProviderRefinement == DL!Contract(P, J!ChosenLog(P, state), state.proposed, state.outputHistory)
Validity ==
    \A q \in state.chosenHistory : \A i \in 1..Len(q.seq) :
      q.seq[i] \in Commands \/ q.seq[i].kind \in {"journal.handoff", "journal.barrier"}
Refinement == projectionOK
OrderRefinement == orderOK
Completes == <>(AllCommandsDelivered /\ RecoveryComplete)

\* Reachability configurations deliberately assert the negation of a path.
NoPublication == ~AllCommandsDelivered
NoEarlyFollower ==
    ~(\E c \in Configs : \E b \in P.ballots :
        \E v \in P.members[c] \ {J!Leader(P, c, b)} :
          Len(state.learned[c][v].seq) >
                     Len(state.learned[c][J!Leader(P, c, b)].seq))
NoResetRecovery == ~(resetDone /\ AllCommandsDelivered /\ RecoveryComplete)
NoHandoff ==
    ~(\E c \in Configs \ {0} : \E q \in state.chosenHistory : q.cfg = c)
NoTwoHandoffs ==
    ~(\E q \in state.chosenHistory : Cardinality(
       {i \in 1..Len(q.seq) : q.seq[i].kind = "journal.handoff"}) >= 2)
NoCompaction == state.compactHistory = {}
NoLostLeaderCompletion == ~(destroyDone /\ AllCommandsDelivered)
=============================================================================
