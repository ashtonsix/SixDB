--------------------------- MODULE TerminalFloor ---------------------------
EXTENDS Contracts, Integers
CONSTANTS Attempts, Bug
Ids == 1..Attempts
VARIABLES opened,network,held,terminal,floor,reference,ready,localFloor,resets,compacted,recoveredCompacted
vars == <<opened,network,held,terminal,floor,reference,ready,localFloor,resets,compacted,recoveredCompacted>>
Init == /\ opened={} /\ network={} /\ held={} /\ terminal={} /\ floor=0
 /\ reference=[i \in Ids |-> "empty"] /\ ready=TRUE /\ localFloor=0 /\ resets=0 /\ compacted=FALSE /\ recoveredCompacted=FALSE
Open == \E i \in Ids \ opened:
 /\ (i=1 \/ i-1 \in opened)
 /\ opened'=opened \cup {i} /\ network'=network \cup {i}
 /\ UNCHANGED <<held,terminal,floor,reference,ready,localFloor,resets,compacted,recoveredCompacted>>
DeliverHold == \E i \in network:
 /\ ready /\ network'=network \ {i}
 /\ held'=(IF i>localFloor /\ i \notin terminal THEN held \cup {i} ELSE held)
 /\ reference'=(IF reference[i]="empty" THEN [reference EXCEPT ![i]="held"] ELSE reference)
 /\ UNCHANGED <<opened,terminal,floor,ready,localFloor,resets,compacted,recoveredCompacted>>
Terminate == \E i \in opened:
 /\ i>localFloor /\ i \notin terminal /\ ready
 /\ terminal'=terminal \cup {i} /\ held'=held \ {i}
 /\ reference'=[reference EXCEPT ![i]="terminal"]
 /\ UNCHANGED <<opened,network,floor,ready,localFloor,resets,compacted,recoveredCompacted>>
Compact == \E n \in (floor+1)..Attempts:
 /\ ready /\ n \in terminal
 /\ (Bug="skip-unresolved" \/ (floor+1)..n \subseteq terminal)
 /\ floor'=n /\ localFloor'=n /\ terminal'=terminal \ (1..n) /\ compacted'=TRUE
 /\ UNCHANGED <<opened,network,held,reference,ready,resets,recoveredCompacted>>
Reset == /\ resets=0 /\ ready /\ ready'=FALSE /\ localFloor'=0 /\ resets'=1
 /\ UNCHANGED <<opened,network,held,terminal,floor,reference,compacted,recoveredCompacted>>
Recover == /\ ~ready /\ ready'=TRUE
 /\ localFloor'=(IF Bug="forget-floor" THEN 0 ELSE floor)
 /\ recoveredCompacted'=(recoveredCompacted \/ floor>0)
 /\ UNCHANGED <<opened,network,held,terminal,floor,reference,resets,compacted>>
PhysicalStatus(i) == IF i<=floor \/ i \in terminal THEN "terminal" ELSE IF i \in held THEN "held" ELSE "empty"
\* reference is the uncompressed holder fold; it never authorizes actions.
RefinesUncompressed == \A i \in Ids:PhysicalStatus(i)=reference[i]
NoResurrection == \A i \in held:reference[i]#"terminal"
Done == opened=Ids /\ network={} /\ \A i \in Ids:reference[i]="terminal"
Next == Open \/ DeliverHold \/ Terminate \/ Compact \/ Reset \/ Recover \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Open) /\ WF_vars(DeliverHold) /\ WF_vars(Terminate) /\ WF_vars(Recover)
Completes == <>Done
NoCompactedRecovery == ~(recoveredCompacted /\ ready /\ Done)
NoHole == ~(2 \in terminal /\ reference[1]#"terminal")
=============================================================================
