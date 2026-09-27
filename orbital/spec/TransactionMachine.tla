------------------------ MODULE TransactionMachine ------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS Scenario, Bug, Crash, TxNone, Service
K == INSTANCE TxKernel
J == INSTANCE DurableLog
O == INSTANCE TransactionOracle
P == Params(Scenario,Bug,Crash)
JP == [owners |-> P.shards, actors |-> {K!Fold(a):a \in P.shards},
       subscribers |-> [a \in P.shards |-> {K!Fold(a)}],
       initialConfig |-> [a \in P.shards |-> 1]]
VARIABLES state, journal, network
vars == <<state,journal,network>>
Init == /\ state=K!Init(P) /\ journal=J!Init(JP) /\ network={}
RECURSIVE EagerFacts(_,_)
EagerFacts(st,es) ==
  IF es = <<>> THEN st
  ELSE IF Head(es).kind="tx.fact"
       THEN LET steps==K!Receive(P,st,Head(es))
            IN EagerFacts(IF steps={} THEN st ELSE (CHOOSE v \in steps:TRUE).next,Tail(es))
       ELSE EagerFacts(st,Tail(es))
After(st,es) == IF Service="direct" THEN EagerFacts(st,es) ELSE st
Forward(es) == IF Service="direct" THEN {e \in Elements(es):e.kind#"tx.fact"} ELSE Elements(es)
KernelStep == \E tr \in K!Actions(P,state):
  /\ state'=After(tr.next,tr.emissions) /\ journal'=journal /\ network'=network \cup Forward(tr.emissions)
JournalStep == /\ Service="full"
              /\ \E tr \in J!Actions(JP,journal):
  /\ journal'=tr.next /\ state'=state /\ network'=network \cup Elements(tr.emissions)
InputStep == \E e \in network:
  \/ /\ Service="full" /\ e.kind \in {"journal.submit","journal.recover"}
     /\ \E tr \in J!Receive(JP,journal,e):
          /\ journal'=tr.next /\ state'=state
          /\ network'=(network \ {e}) \cup Elements(tr.emissions)
  \/ /\ Service \in {"folded","direct"} /\ e.kind="journal.submit"
     /\ \E tr \in K!CommitAndFold(P,state,e.body):
            /\ state'=After(tr.next,tr.emissions) /\ journal'=journal
            /\ network'=(network \ {e}) \cup Forward(tr.emissions)
  \/ /\ e.kind \in {"journal.deliver","journal.snapshot","tx.fact","root.cut-protected"}
     /\ \E tr \in K!Receive(P,state,e):
          /\ state'=tr.next /\ journal'=journal
          /\ network'=(network \ {e}) \cup Elements(tr.emissions)
  \/ /\ e.kind="tx.published"
     /\ state'=state /\ journal'=journal /\ network'=network \ {e}
Done == state.published=P.transactions
Terminal == Done /\ network={} /\ UNCHANGED vars
Next == KernelStep \/ JournalStep \/ InputStep \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(KernelStep) /\ WF_vars(JournalStep) /\ WF_vars(InputStep)
Completes == <>Done

CommandIdentity == ~state.commandClash
JournaledProfile == \A t \in state.begun:
 \E id \in DOMAIN state.commands:
   /\ state.commands[id].kind="begin" /\ state.commands[id].body.tx=t
   /\ state.profiles[t]=state.commands[id].body.data.profile
OnlyDeclaredBounds == \A k \in P.keys:state.bounds[k]=K!MaxN({0} \cup
 UNION {{state.readCut[t][r]:r \in {x \in state.registered[t]:k \in P.dependencies[x]}}:t \in P.transactions})
PolicyContext == \A t \in P.transactions:state.outcome[t]#TxNone =>
 state.outcome[t].context[3].verification=
   IF t \in P.checked THEN "native-checked" ELSE IF P.approved \/ P.asyncCheck THEN "native-approved" ELSE "wasm"
PositionImmutable == \A t \in P.transactions:
  \A a \in P.parts[t]:state.fixed[t][a]>0 => state.fixed[t][a]=state.position[t]
UniquePositions == \A t,u \in P.transactions:
  t#u /\ state.position[t]>0 /\ state.position[u]>0 => state.position[t]#state.position[u]
ExclusiveReservations == \A t,u \in P.transactions: \A a \in P.shards:
  t#u /\ K!Holds(state,t,a) /\ K!Holds(state,u,a) => ~K!Conflict(P,t,u,a)
GrantOrderPositions == \A t,u \in P.transactions: \A a \in P.shards:
  (K!At(state.grantOrder[a],t)>0 /\ K!At(state.grantOrder[a],t)<K!At(state.grantOrder[a],u) /\
   K!Conflict(P,t,u,a) /\ state.position[t]>0 /\ state.position[u]>0) => state.position[t]<state.position[u]
NoLocalResurrection == \A a \in P.shards: \A t \in state.localCancel[a]:
  state.ticket[t][a]="resolved"
RegisteredBeforeRead == \A r \in state.observations:
  r.key \in state.registered[r.tx] /\ state.bounds[r.key]>=r.position
NoEarlyExecution == \A t \in P.transactions:state.private[t]#TxNone =>
  \A a \in P.parts[t]:state.fixed[t][a]=state.position[t]
DecisionEvidence == \A t \in P.transactions:state.decision[t]="commit" =>
  /\ state.outcome[t]#TxNone /\ state.outcome[t].status="ok"
  /\ \A a \in P.parts[t]:state.fixed[t][a]=state.position[t]
AllChecks == \A t \in P.checked:state.decision[t]="commit" =>
  state.reports[t][1]#TxNone /\ state.reports[t][1]=state.reports[t][2] /\
  state.reports[t][1].outcome=state.outcome[t]
SerialReads == O!ObservedAtPosition(P,state)
SerialOutcomes == O!ResultSemantics(P,state)
JustifiedOutcome == O!OutcomeJustified(P,state)
Publication == O!PublishedSemantics(P,state)
NoPublication == state.published={}
NoPartialFix == ~(state.fixed[1][1]>0 /\ state.fixed[1][2]=0)
NoPartialInstall == ~(1 \in state.resolved[1] /\ 1 \notin state.resolved[2])
NoCrossedCancellation == ~(state.cancelled[1] /\ \E a \in P.shards:K!Holds(state,1,a))
NoRecoveryCompletion == ~(state.crashes>0 /\ Done)
NoReplacementBenefit == ~(Scenario="chain" /\ 4 \in state.published /\ state.decision[1]=TxNone /\
  state.decision[2]=TxNone /\ state.decision[3]="commit" /\
  state.position[3]<state.position[4] /\ state.inputs[4][1]=9)
Causality == \A t \in P.transactions:state.position[t]>0 => state.position[t]>P.causal[t]
OwnEffects == \A t \in P.transactions:P.program[t]="own-read" /\ state.outcome[t]#TxNone =>
  state.outcome[t].trace= <<<<"query",P.firstRead[t],P.value[t],K!Context(P,state,t)>> >>
Envelope == \A t \in P.transactions:state.outcome[t]#TxNone => DOMAIN state.outcome[t].effects \subseteq P.writes[t]
AsyncStable == \A t \in state.incidents:state.decision[t]="commit" /\ t \in state.published
IncidentSound == \A t \in state.incidents:state.asyncReports[t]#TxNone /\
  (state.asyncReports[t].result#state.outcome[t].result \/ state.asyncReports[t].context#state.outcome[t].context)
MapTransferSafe == state.migrated => state.oldMapPlans \subseteq state.published
NoAsyncIncident == state.incidents={}
NoMapTransfer == ~state.migrated
NoLateRead == ~(3 \in P.transactions /\ state.inputs[3][P.secondRead[3]]#TxNone /\ state.inputs[3][1]#TxNone)
NoMismatchAbort == ~(2 \in P.checked /\ state.decision[2]="abort" /\ state.outcome[2]#TxNone /\ state.outcome[2].status="mismatch")
NoApprovedPublication == ~\E t \in state.published:
 state.decision[t]="commit" /\ state.outcome[t].context[3].verification="native-approved" /\
 state.reports[t][1]=TxNone /\ state.reports[t][2]=TxNone
=============================================================================
