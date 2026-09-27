-------------------------- MODULE AdmissionEvidence --------------------------
EXTENDS Contracts, Integers
CONSTANT Bug
VARIABLES state,journal,network,refines,everTogether
vars == <<state,journal,network,refines,everTogether>>
M == INSTANCE AdmissionMaterial WITH Streams<-1,Entries<-1,Holders<-2,
 Generations<-1,Losses<-2,Reset<-FALSE,Bug<-Bug,Service<-"folded",Atomic<-TRUE
A == INSTANCE AdmissionKernel
Key == <<1,1>>
Together(s) == A!HasCopy(s,1,Key) /\ A!HasCopy(s,2,Key)
Init == M!Init /\ everTogether=FALSE
Next == M!Next /\ everTogether'=(everTogether \/ Together(state'))
Spec == Init /\ [][Next]_vars
\* Historical evidence is not a statement that both copies ever coexisted.
NoHistoricalOnlyCertificate == ~(state.certificates#{} /\ ~everTogether)
\* Beyond the declared allowance, valid historic evidence can coexist with loss.
NoExhaustedProtection == ~(Key \in state.promised /\
 state.lost-state.begins[1]>1 /\ ~A!HasCopy(state,1,Key) /\ ~A!HasCopy(state,2,Key))
Protection == M!Protection
CertificateEvidence == M!CertificateEvidence
=============================================================================
