---------------------------- MODULE EpochExecution ----------------------------
EXTENDS Contracts, Integers
CONSTANTS TxNone, Bug, LastEpoch, Mismatch
C == INSTANCE TxProgram
P == [transactions |-> 1..3, keys |-> 1..3,
      writes |-> [t \in 1..3 |-> IF t=3 THEN {2} ELSE {1}],
      reads |-> [t \in 1..3 |-> IF t=3 THEN {} ELSE IF t=2 THEN {1,3} ELSE {1}],
      program |-> [t \in 1..3 |-> IF t=3 THEN "put" ELSE IF t=2 THEN "conditional" ELSE "increment"],
      firstRead |-> [t \in 1..3 |-> 1], secondRead |-> [t \in 1..3 |-> 3],
      value |-> [t \in 1..3 |-> 7]]
Replicas == {1,2}
Context(t) == <<t,4*t,"profile-1",1>>
Conflicts(t,u) == P.writes[t] \cap (P.writes[u] \cup P.reads[u]) # {} \/
                  P.reads[t] \cap P.writes[u] # {}
RInit == [epoch |-> 1, started |-> {}, programs |-> [t \in 1..3 |-> TxNone],
          stage |-> [t \in 1..3 |-> "cold"],
          values |-> [k \in 1..3 |-> 0], decision |-> [t \in 1..3 |-> TxNone],
          published |-> {}, reports |-> [t \in 1..3 |-> <<>>],
          metadata |-> [k \in 1..3 |-> 0], outbox |-> {},
          closed |-> [e \in 1..LastEpoch |-> TxNone]]
VARIABLE replicas
vars == <<replicas>>
Init == replicas=[r \in Replicas |-> RInit]
Startable(s,t) == t \notin s.started /\
   (Bug="commuting-deltas" \/ \A u \in 1..(t-1):Conflicts(t,u) => s.decision[u]#TxNone)
Steppable(s,t) == t \in s.started /\ s.programs[t].todo# <<>>
Decidable(s,t) == t \in s.started /\ s.programs[t].todo= <<>> /\ s.decision[t]=TxNone /\
                   (t#1 \/ s.epoch=LastEpoch \/ Bug="early-publication")
LogicalWork(s) == {t \in 1..3:Startable(s,t) \/ Steppable(s,t) \/ Decidable(s,t)}
Projection(s) == [values |-> s.values,decision |-> s.decision,programs |-> s.programs,
                   metadata |-> s.metadata,outbox |-> s.outbox,published |-> s.published,
                   pending |-> {t \in 1..3:s.decision[t]=TxNone},reports |-> s.reports]

(* Capture/materialize/execute are separately schedulable physical stages.
   The reference dependency relation contains read observations and metadata,
   not merely a test that final write deltas commute. *)
Start(r,t) ==
  LET s==replicas[r]
  IN /\ s.closed[s.epoch]=TxNone /\ Startable(s,t)
     /\ replicas'=[replicas EXCEPT ![r].started=@ \cup {t},
           ![r].programs[t]=C!Init(P,t,[k \in P.reads[t] |-> s.values[k]],Context(t)),
           ![r].metadata=[k \in 1..3 |-> IF k \in P.reads[t] THEN 4*t ELSE @ [k]],
           ![r].stage[t]="captured"]
Materialize(r,t) ==
  /\ replicas[r].stage[t]="captured"
  /\ replicas'=[replicas EXCEPT ![r].stage[t]="ready"]
Execute(r,t) ==
  LET s==replicas[r]
  IN /\ s.closed[s.epoch]=TxNone /\ Steppable(s,t) /\ s.stage[t]="ready"
     /\ replicas'=[replicas EXCEPT ![r].programs[t]=C!Step(P,t,s.programs[t])]
Decide(r,t) ==
  LET s==replicas[r] o==C!Outcome(s.programs[t])
      decision==IF t=1 /\ Mismatch THEN "abort" ELSE "commit"
      reports==IF t=1 THEN <<[context |-> Context(t),request |-> "a",outcome |-> o],
                            [context |-> Context(t),request |-> IF Mismatch THEN "b" ELSE "a",outcome |-> o]>> ELSE <<>>
  IN /\ s.closed[s.epoch]=TxNone /\ Decidable(s,t)
     /\ replicas'=[replicas EXCEPT ![r].decision[t]=decision,
          ![r].reports[t]=reports, ![r].published=@ \cup {t},
          ![r].values=[k \in 1..3 |-> IF decision="commit" /\ k \in DOMAIN o.effects THEN o.effects[k] ELSE @ [k]],
          ![r].outbox=IF decision="commit" THEN @ \cup Elements(o.outbox) ELSE @]
Close(r) ==
  LET s==replicas[r]
  IN /\ s.closed[s.epoch]=TxNone
     /\ IF Bug="physical-closure"
         THEN {t \in LogicalWork(s):~Steppable(s,t) \/ s.stage[t]="ready"}={}
         ELSE LogicalWork(s)={}
     /\ replicas'=[replicas EXCEPT ![r].closed[s.epoch]=IF Bug="drop-continuation" /\ s.epoch<LastEpoch
             THEN [Projection(s) EXCEPT !.programs[1]=TxNone] ELSE Projection(s)]
Advance(r) ==
  LET s==replicas[r]
  IN /\ s.closed[s.epoch]#TxNone /\ s.epoch<LastEpoch
     /\ replicas'=[replicas EXCEPT ![r].epoch=@+1]
Done == \A r \in Replicas:replicas[r].closed[LastEpoch]#TxNone
Next == (\E r \in Replicas:
          (\E t \in 1..3:Start(r,t) \/ Materialize(r,t) \/ Execute(r,t) \/ Decide(r,t)) \/ Close(r) \/ Advance(r))
        \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ \A r \in Replicas:
  /\ WF_vars(Close(r)) /\ WF_vars(Advance(r))
  /\ \A t \in 1..3:WF_vars(Start(r,t)) /\ WF_vars(Materialize(r,t)) /\ WF_vars(Execute(r,t)) /\ WF_vars(Decide(r,t))
Completes == <>Done

(* Independent fixture oracle: its expected serial return/effect sequence is
   written directly and does not invoke C!Step/Run/Evaluate. *)
ExpectedResult(t) == IF t=3 THEN 7 ELSE IF t=1 \/ Mismatch THEN 1 ELSE 2
ExpectedQuery(t) == IF t=1 \/ Mismatch THEN 0 ELSE 1
CorrectPrograms == \A r \in Replicas: \A t \in replicas[r].started:
  replicas[r].programs[t].todo= <<>> =>
    /\ replicas[r].programs[t].result=ExpectedResult(t)
    /\ replicas[r].programs[t].effects=[k \in P.writes[t] |-> ExpectedResult(t)]
    /\ replicas[r].programs[t].context=Context(t)
    /\ t#3 => replicas[r].programs[t].trace=
         <<<<"query",1,ExpectedQuery(t),Context(t)>> >> \o
         (IF t=2 THEN <<<<"query",IF Mismatch THEN 3 ELSE 1,ExpectedQuery(t),Context(t)>> >> ELSE <<>>) \o
         <<<<"own",1,ExpectedResult(t),Context(t)>> >>
IndependentPublication == \A r \in Replicas: \A e \in 1..(LastEpoch-1):
  replicas[r].closed[e]#TxNone => replicas[r].closed[e].published={3}
NoSpeculativePublication == \A r \in Replicas:1 \in replicas[r].published =>
  replicas[r].epoch=LastEpoch
LogicalClosure == \A r \in Replicas:replicas[r].closed[replicas[r].epoch]#TxNone => LogicalWork(replicas[r])={}
ContinuationRetained == \A r \in Replicas: \A e \in 1..(LastEpoch-1):
  replicas[r].closed[e]#TxNone => replicas[r].closed[e].programs[1]#TxNone
Deterministic == \A e \in 1..LastEpoch:
  replicas[1].closed[e]#TxNone /\ replicas[2].closed[e]#TxNone => replicas[1].closed[e]=replicas[2].closed[e]
OrderingMetadata == \A r \in Replicas: \A e \in 1..LastEpoch:
  replicas[r].closed[e]#TxNone => replicas[r].closed[e].metadata=
    [k \in 1..3 |-> IF k=2 THEN 0 ELSE IF e=LastEpoch THEN 8 ELSE IF k=1 THEN 4 ELSE 0]
OutboxSemantics == \A r \in Replicas:replicas[r].outbox=
   {[id |-> <<Context(t),"result",0>>,value |-> ExpectedResult(t)]:
       t \in {u \in replicas[r].published:replicas[r].decision[u]="commit"}}
NoDynamicBranch == \A r \in Replicas:2 \in replicas[r].started => Cardinality(replicas[r].programs[2].children)<2
NoPhysicalSkew == ~(replicas[1].closed[1]#TxNone /\ replicas[2].closed[1]=TxNone /\ replicas[2].stage[1]="captured")
NoCompletion == ~Done
NoPendingThenResume == ~Done \/ replicas[1].closed[1].pending# {1,2}
=============================================================================
