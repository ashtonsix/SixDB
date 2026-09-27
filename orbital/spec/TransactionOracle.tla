------------------------- MODULE TransactionOracle -------------------------
EXTENDS Integers, FiniteSets, Sequences
CONSTANT TxNone

(* Application fixture and independent serial semantics. None of these
   observers is used by TxKernel to authorize a protocol action. Values are
   actual small integers, not hashes or completion flags. *)
MaxOf(xs) == CHOOSE x \in xs : \A y \in xs : x >= y
LastBefore(versions, key, cut) ==
  LET eligible == {v \in versions : v.key = key /\ v.position <= cut}
  IN IF eligible = {} THEN 0
     ELSE (CHOOSE v \in eligible : v.position = MaxOf({w.position : w \in eligible})).value

ExpectedInput(p, s, t, key) ==
  LET before == {u \in p.transactions :
          s.position[u] > 0 /\ s.position[u] < s.position[t] /\
          s.decision[u] = "commit" /\ key \in DOMAIN s.outcome[u].effects}
  IN IF before = {} THEN 0
     ELSE LET u == CHOOSE u \in before :
                    s.position[u] = MaxOf({s.position[w] : w \in before})
          IN s.outcome[u].effects[key]

ProgramEffects(p, t, inputs) ==
  CASE p.program[t] \in {"put","own-read"} -> [k \in p.writes[t] |-> p.value[t]]
    [] p.program[t] \in {"none","undeclared"} -> [k \in {} |-> 0]
    [] p.program[t] = "increment" -> [k \in p.writes[t] |-> inputs[p.firstRead[t]] + 1]
    [] p.program[t] = "sum" -> [k \in p.writes[t] |-> inputs[p.firstRead[t]] + inputs[p.secondRead[t]]]
    [] p.program[t] = "read" -> [k \in {} |-> 0]
    [] OTHER -> [k \in p.writes[t] |-> p.value[t]]

ProgramResult(p, t, inputs) ==
  CASE p.program[t] = "read" -> inputs[p.firstRead[t]]
    [] p.program[t] = "increment" -> inputs[p.firstRead[t]] + 1
    [] p.program[t] = "sum" -> inputs[p.firstRead[t]] + inputs[p.secondRead[t]]
    [] OTHER -> p.value[t]

ObservedAtPosition(p, s) ==
  \A r \in s.observations : r.value = ExpectedInput(p, s, r.tx, r.key)

ResultSemantics(p, s) ==
  \A t \in p.transactions : s.outcome[t] # TxNone /\ s.outcome[t].status = "ok" =>
    LET inputs == [k \in p.reads[t] |-> ExpectedInput(p, s, t, k)]
    IN /\ s.outcome[t].effects = ProgramEffects(p, t, inputs)
       /\ s.outcome[t].result = ProgramResult(p, t, inputs)

FailureEvidence(p,s,t) ==
  CASE s.outcome[t].status="mismatch" ->
    /\ t \in p.checked /\ s.reports[t][1]#TxNone /\ s.reports[t][2]#TxNone
    /\ s.executionEvidence[t]#TxNone
    /\ (s.reports[t][1]#s.reports[t][2] \/
       \E r \in {1,2}:s.reports[t][r].outcome#s.executionEvidence[t])
  [] s.outcome[t].status="unavailable" ->
    /\ s.failedReads[t]#{}
    /\ \A k \in s.failedReads[t]:\E e \in s.evidence:
        e.tx=t /\ e.key=k /\ e.cut=s.position[t]
  [] s.outcome[t].status="bad-envelope" ->
    p.program[t]="undeclared" \/
    (s.executionErrors[t]#TxNone /\ ~(DOMAIN s.executionErrors[t].effects \subseteq p.writes[t]))
  [] OTHER -> FALSE

OutcomeJustified(p, s) ==
  \A t \in p.transactions : s.decision[t] # TxNone =>
    IF s.cancelled[t] THEN s.decision[t] = "abort"
    ELSE /\ s.outcome[t] # TxNone
         /\ s.decision[t] = (IF s.outcome[t].status = "ok" THEN "commit" ELSE "abort")
         /\ (s.outcome[t].status="ok" \/ FailureEvidence(p,s,t))

PublishedSemantics(p, s) ==
  \A t \in s.published :
    /\ s.decision[t] # TxNone
    /\ \A shard \in p.parts[t] : t \in s.resolved[shard]
    /\ s.decision[t] = "commit" =>
       \A k \in DOMAIN s.outcome[t].effects :
         \E v \in s.versions : v.tx = t /\ v.key = k /\
            v.position = s.position[t] /\ v.value = s.outcome[t].effects[k]
=============================================================================
