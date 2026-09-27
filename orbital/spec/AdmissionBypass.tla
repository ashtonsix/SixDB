--------------------------- MODULE AdmissionBypass ---------------------------
EXTENDS Contracts, Integers
CONSTANTS Scenario, Bug, TxNone
VARIABLES admission,anet,aphase,arefines,early,repair,tail,tx,journal,tnet,bypassed,apulse
vars == <<admission,anet,aphase,arefines,early,repair,tail,tx,journal,tnet,bypassed,apulse>>
AI == INSTANCE AdmissionHistories WITH Shape <- "same",Bug <- "none",state <- admission,
 network <- anet,phase <- aphase,refines <- arefines,earlyCertificate <- early,repairLoss <- repair,tailFound <- tail,pulse <- apulse
TI == INSTANCE TransactionMachine WITH Scenario <- Scenario,Bug <- "none",Crash <- FALSE,
 Service <- "direct",state <- tx,network <- tnet,journal <- journal
Init == AI!Init /\ TI!Init /\ bypassed=FALSE
Hole == admission.certified[<<1,2>>]#{} /\ admission.frontier[1]=0
AdmissionStep == /\ ~Hole /\ AI!ProtocolNext
 /\ UNCHANGED <<tx,journal,tnet,bypassed>>
TransactionStep == /\ Hole /\ (Bug#"global-source-order" \/ admission.frontier[1]=AI!P.entries) /\ TI!Next
 /\ bypassed'=(bypassed \/ tx'.published=TI!P.transactions)
 /\ UNCHANGED <<admission,anet,aphase,arefines,early,repair,tail,apulse>>
\* LSN 1 remains absent from this authored workload. C work is independently
\* eligible and must finish while that unrelated stream gap remains unresolved.
Done == TI!Done
Heartbeat == AI!Heartbeat /\ UNCHANGED <<tx,journal,tnet,bypassed>>
Next == AdmissionStep \/ TransactionStep \/ Heartbeat \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(AdmissionStep) /\ WF_vars(TransactionStep)
Serial == TI!SerialReads /\ TI!SerialOutcomes /\ TI!Publication
SourceContiguous == AI!Contiguous
Completes == <>Done
SourceIndependent == <>bypassed
NoBypass == ~bypassed
=============================================================================
