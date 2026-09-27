------------------------ MODULE AdmissionInertLossAudit ------------------------
EXTENDS AdmissionInertCorrespondence
\* An authored lossful fine-state audit, not the unrestricted target product.
\* Both duplicate packets come from real Forward actions by holder 2. The first
\* repairs holder 1 after loss; the second can be erased only after that holder's
\* loss budget is exhausted. Every visited state audits all kernel/input successors.
VARIABLE phase, preLoss, afterDomainLoss, afterBudget
auditvars == <<state,journal,network,refines,phase,preLoss,afterDomainLoss,afterBudget>>
Key == <<1,1>>
AuditInit == Init /\ phase=0 /\ preLoss=FALSE /\ afterDomainLoss=FALSE /\ afterBudget=FALSE
Take(t,remaining,nextPhase) ==
 /\ state'=t.next /\ network'=remaining \cup Elements(t.emissions)
 /\ refines'=(refines /\ C!Step(P,C!Project(state),C!Project(t.next)) /\ C!Emissions(P,state,t.emissions))
 /\ phase'=nextPhase /\ UNCHANGED journal
Keep(t) ==
 /\ t.tag \notin {"destroy-storage-domain","producer-disappears"}
 /\ (t.tag#"producer-submission" \/ phase#2)
 /\ (t.tag#"payload-forward" \/ t.emissions[1].src=1)
 /\ (IF t.tag="admission-propose"
      THEN t.emissions[1].body.kind#"admission.protect" \/
           t.emissions[1].body.body.generation=1 \/ phase>=3
      ELSE TRUE)
Rank(tag) == CASE tag="admission-propose" -> 0
 [] tag="send-protection-request" -> 1 [] tag="persist-immutable-package" -> 2
 [] tag="persist-protection-generation" -> 3 [] tag="producer-submission" -> 4
 [] tag="payload-forward" -> 5 [] tag="durability-receipt" -> 6 [] OTHER -> 7
Moves == {t \in A!Actions(P,state):Keep(t)}
Selected == {t \in Moves:~(\E u \in Moves:Rank(u.tag)<Rank(t.tag))}
Move == \E t \in Selected:
 /\ Take(t,network,phase) /\ UNCHANGED <<preLoss,afterDomainLoss,afterBudget>>
Deliver == \E e \in network:
 /\ IF e.kind="journal.submit"
    THEN Take(Transition("fold",A!Apply(P,state,e.body),<<>>),network \ {e},phase)
    ELSE \E t \in A!Receive(P,state,e):Take(t,network \ {e},phase)
 /\ UNCHANGED <<preLoss,afterDomainLoss,afterBudget>>
QueueDuplicate ==
 /\ network={}
 /\ (phase=0 /\ state.promised=A!Keys(P)) \/ (phase=3 /\ Done)
 /\ \E t \in A!Forward(P,state,2,Key,1,state.generation):
     LET e == t.emissions[1]
     IN /\ Take(t,network,IF phase=0 THEN 1 ELSE 4)
        /\ preLoss'=(preLoss \/ (phase=0 /\ A!HasCopy(state,1,Key) /\ ~Q!Dead(P,t.next,e)))
        /\ afterDomainLoss'=(afterDomainLoss \/ (phase=3 /\ Q!Dead(P,t.next,e)))
 /\ UNCHANGED afterBudget
Loss ==
 /\ phase \in {1,4}
 /\ \E t \in A!Lose(P,state,IF phase=1 THEN 1 ELSE 2):
     /\ Take(t,network,IF phase=1 THEN 2 ELSE 5)
     /\ afterBudget'=(afterBudget \/ (phase=4 /\ t.next.lost=P.losses /\
          \E e \in network:Q!Dead(P,t.next,e)))
 /\ UNCHANGED <<preLoss,afterDomainLoss>>
Repaired ==
 /\ phase=2 /\ A!HasCopy(state,1,Key)
 /\ phase'=3 /\ UNCHANGED <<state,journal,network,refines,preLoss,afterDomainLoss,afterBudget>>
AuditDone == phase=5 /\ Done /\
 \A h \in A!Holders(P):A!HasCopy(state,h,Key) /\ state.disk[h][Key].generation=2
AuditNext ==
 IF ENABLED QueueDuplicate THEN QueueDuplicate
 ELSE IF ENABLED Loss THEN Loss
 ELSE IF ENABLED Repaired THEN Repaired
 ELSE IF network#{} THEN Deliver
 ELSE Move \/ (AuditDone /\ UNCHANGED auditvars)
AuditSpec == AuditInit /\ [][AuditNext]_auditvars /\ WF_auditvars(AuditNext)
AuditCompletes == <>AuditDone
CutCoverage == AuditDone => preLoss /\ afterDomainLoss /\ afterBudget /\ state.begins[2]=1
NoAuditCut == ~AuditDone
=============================================================================
