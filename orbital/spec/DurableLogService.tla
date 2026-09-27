-------------------------- MODULE DurableLogService ---------------------------
EXTENDS Contracts
CONSTANT Mode
D == INSTANCE DurableLog
P == [owners |-> {"owner"},actors |-> {"reader"},
      subscribers |-> [j \in {"owner"} |-> {"reader"}],
      initialConfig |-> [j \in {"owner"} |-> 1]]
Value == Command("owner","same-command","fixture",[value |-> 7])
Inputs == {Event(i,"producer","owner","journal.submit",Value):i \in {1,2}}
Recovery == Event("recover","reader","owner","journal.recover",[reason |-> "restart"])
VARIABLES state, offered, output, recovered
vars == <<state,offered,output,recovered>>
Init == /\ state=D!Init(P) /\ offered={} /\ output={} /\ recovered=FALSE
Submit == \E e \in Inputs \ offered:
  \E tr \in D!Receive(P,state,e):
    /\ state'=tr.next /\ offered'=offered \cup {e}
    /\ UNCHANGED <<output,recovered>>
Recover == /\ offered=Inputs /\ ~recovered /\ Len(state.log["owner"])=2
           /\ \E tr \in D!Receive(P,state,Recovery):state'=tr.next
           /\ recovered'=TRUE /\ UNCHANGED <<offered,output>>
Service == \E tr \in D!Actions(P,state):
  /\ state'=tr.next /\ output'=output \cup Elements(tr.emissions)
  /\ UNCHANGED <<offered,recovered>>
Done == recovered /\ state.pending={} /\ state.recoveries={} /\
        state.delivered["reader"]["owner"]=Len(state.log["owner"])
Next == Submit \/ Recover \/ Service \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Submit) /\ WF_vars(Recover) /\ WF_vars(Service)
Sound == \A e \in output:D!DeliverySound(state.log,e)
Valid == \A c \in Elements(state.log["owner"]):c=Value \/ c.kind="journal.barrier"
NoDuplicatePositions == ~\E i,j \in 1..Len(state.log["owner"]):
  i#j /\ state.log["owner"][i]=Value /\ state.log["owner"][j]=Value
NoRecoverySnapshot == ~\E e \in output:e.kind="journal.snapshot" /\ e.body.index=3
Completes == <>Done
=============================================================================
