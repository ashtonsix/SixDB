--------------------------- MODULE RecipeDependencies ---------------------------
EXTENDS Contracts, Integers
CONSTANTS Cycle, Bug
M == INSTANCE MaterialCore
Nodes == {"source","left","right"}
Edges == [n \in Nodes |-> CASE n="source" -> {} [] n="left" -> {IF Cycle THEN "right" ELSE "source"} [] OTHER -> {"left"}]
VARIABLES data,registered,failed
vars == <<data,registered,failed>>
Init == /\ data=[n \in {"source"} |-> <<7>>] /\ registered={} /\ failed={}
\* Dependency descriptions alone produce no material. A decoder can construct
\* this fixture only from a physically present child's bytes; source is durable.
Buildable == {n \in Nodes \ DOMAIN data:Edges[n]#{} /\ Edges[n] \subseteq DOMAIN data}
Build == \E n \in Buildable:
 /\ data'=data @@ (n :> data[CHOOSE child \in Edges[n]:TRUE])
 /\ UNCHANGED <<registered,failed>>
Register == \E n \in Nodes \ (registered \cup failed):
 /\ (n \in DOMAIN data \/ Bug="declaration-is-material")
 /\ registered'=registered \cup {n} /\ UNCHANGED <<data,failed>>
FailClosed == /\ Buildable={} /\ Nodes \ (DOMAIN data \cup failed)#{}
 /\ failed'=failed \cup (Nodes \ DOMAIN data) /\ UNCHANGED <<data,registered>>
Done == registered \cup failed=Nodes
Next == Build \/ Register \/ FailClosed \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Build) /\ WF_vars(Register) /\ WF_vars(FailClosed)
GroundedRoots == registered \subseteq M!Resolved(Nodes,Edges,{"source"},Cardinality(Nodes))
ActualBytes == registered \subseteq DOMAIN data /\ \A n \in DOMAIN data:data[n]= <<7>>
FailureJustified == \A n \in failed:n \notin M!Resolved(Nodes,Edges,{"source"},Cardinality(Nodes))
Completes == <>Done
NoCycleFailure == ~(Cycle /\ {"left","right"} \subseteq failed /\ "source" \in registered)
NoDerivedSuccess == ~(~Cycle /\ registered=Nodes)
=============================================================================
