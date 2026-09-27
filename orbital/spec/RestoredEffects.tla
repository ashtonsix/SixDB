---------------------------- MODULE RestoredEffects ----------------------------
EXTENDS RecoveryCut
CONSTANT Dedup
E == INSTANCE EffectKernel
EP == [owner |-> "effects",actor |-> "effect-driver",dedup |-> Dedup,
       reset |-> TRUE,restore |-> TRUE,bad |-> Bug]
EJP == [owners |-> {EP.owner},actors |-> {EP.actor},
        subscribers |-> [o \in {EP.owner} |-> {EP.actor}],
        initialConfig |-> [o \in {EP.owner} |-> 1]]
VARIABLES effect,effectJournal,effectNet,dropped,openedReceived
allvars == <<vars,effect,effectJournal,effectNet,dropped,openedReceived>>
JoinedInit == Init /\ effect=E!Init(EP) /\ effectJournal=J!Init(EJP) /\ effectNet={}
              /\ dropped=FALSE /\ openedReceived=FALSE

\* Reuse the actual partial-install checkpoint, persisted decoder, journal
\* suffix recovery, closed-cut reconstruction and external fence from C9.
\* These are authored causal cuts, not every product interleaving: one original
\* committed effect reaches the sink and loses its reply before the disaster;
\* the later transaction's effect remains pending across the selected cut.
FirstId == IF tx.outcome[1]=TxNone THEN "unavailable" ELSE ToString(tx.outcome[1].outbox[1].id)
FirstApplied == FirstId \in DOMAIN effect.applications /\ effect.applications[FirstId]>0
AllIntentsDurable == Cardinality(DOMAIN effect.intents)=Cardinality(P.transactions)
CanDisaster == FirstApplied /\ dropped /\ AllIntentsDurable /\ effectNet={}
CarrierAvailable == \E e \in network:e.kind \in {"tx.published","cut.opened"}
Capture == \E e \in {v \in network:v.kind \in {"tx.published","cut.opened"}}:
 /\ (e.kind#"cut.opened" \/ effect.reset)
 /\ \E tr \in E!Receive(EP,effect,e):
      /\ effect'=tr.next /\ effectNet'=effectNet \cup Elements(tr.emissions)
      /\ openedReceived'=(openedReceived \/ e.kind="cut.opened")
      /\ network'=network \ {e}
      /\ UNCHANGED <<tx,cut,journal,phase,restored,externalDead,oldTried,effectJournal,dropped>>
RecoveryLocal == TxStep \/ Offer \/ Continue \/ (CanDisaster /\ Disaster) \/
                 Rebuild \/ ExternalFence \/ OldWrite \/ CutStep
RecoveryStep ==
 /\ ~CarrierAvailable
 /\ (Deliver \/ (~ENABLED Deliver /\ JournalStep) \/
      (~ENABLED Deliver /\ ~ENABLED JournalStep /\ RecoveryLocal))
 /\ UNCHANGED <<effect,effectJournal,effectNet,dropped,openedReceived>>
EffectActions == {tr \in E!Actions(EP,effect):
 /\ (tr.tag#"effect-owner-loss" \/ phase=3)
 /\ (tr.tag#"effect-propose" \/
       Head(tr.emissions).body.kind#"effect.dispatch" \/
       openedReceived \/ Head(tr.emissions).body.body.id=FirstId)}
EffectStep == \E tr \in EffectActions:
 /\ effect'=tr.next /\ effectNet'=effectNet \cup Elements(tr.emissions)
 /\ UNCHANGED <<vars,effectJournal,dropped,openedReceived>>
EffectJournalStep == \E tr \in J!Actions(EJP,effectJournal):
 /\ effectJournal'=tr.next /\ effectNet'=effectNet \cup Elements(tr.emissions)
 /\ UNCHANGED <<vars,effect,dropped,openedReceived>>
EffectDeliver == \E e \in effectNet:
 \/ /\ e.kind \in {"journal.submit","journal.recover"}
    /\ \E tr \in J!Receive(EJP,effectJournal,e):
       /\ effectJournal'=tr.next /\ effectNet'=(effectNet \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<vars,effect,dropped,openedReceived>>
 \/ /\ e.kind \notin {"journal.submit","journal.recover"}
    /\ (e.kind#"effect.reply" \/ dropped)
    /\ \E tr \in E!Receive(EP,effect,e):
       /\ effect'=tr.next /\ effectNet'=(effectNet \ {e}) \cup Elements(tr.emissions)
       /\ UNCHANGED <<vars,effectJournal,dropped,openedReceived>>
LoseFirstReply == \E e \in effectNet:
 /\ ~dropped /\ e.kind="effect.reply"
 /\ effectNet'=effectNet \ {e} /\ dropped'=TRUE
 /\ UNCHANGED <<vars,effect,effectJournal,openedReceived>>
\* Drain normal journal/message service; dispatch, sink application, reset,
\* physical checkpoint work and recovery context remain separately represented.
Delivery == Capture \/ EffectDeliver \/ LoseFirstReply
Durability == EffectJournalStep
LocalJoined == RecoveryStep \/ EffectStep
JoinedService == Delivery \/ (~ENABLED Delivery /\ Durability) \/
                 (~ENABLED Delivery /\ ~ENABLED Durability /\ LocalJoined)
JoinedDone == Done /\ openedReceived /\ effect.reset /\ effect.up /\
              E!Resolved(EP,effect) /\ effectNet={}
JoinedNext == JoinedService \/ (JoinedDone /\ UNCHANGED allvars)
JoinedSpec == JoinedInit /\ [][JoinedNext]_allvars /\ WF_allvars(JoinedService)
JoinedCompletes == <>JoinedDone
AtMostOnce == E!AtMostOnce(EP,effect)
CommittedOnly == E!CommittedOnly(EP,effect) /\
 \A id \in DOMAIN effect.applications:
  \E t \in P.transactions:tx.decision[t]="commit" /\ t \in tx.published /\
     id \in {ToString(o.id):o \in Elements(tx.outcome[t].outbox)}
NoNewIntentIdentity == \A id \in DOMAIN effect.intents:
 id \in {b.id:b \in effect.received}
AbandonedStayPending == Mode="pitr" /\ openedReceived =>
 \A id \in effect.abandoned:id \notin DOMAIN effect.applications
KnownResults == \A id \in DOMAIN effect.intents:
 effect.intents[id].phase="acknowledged" => id \in DOMAIN effect.applications /\ effect.applications[id]=1
AmbiguityPreserved == ~Dedup /\ JoinedDone => FirstId \in effect.unknown
FreshIntentCompletes == Mode="normal" /\ JoinedDone =>
 \A id \in DOMAIN effect.intents \ {FirstId}:effect.intents[id].phase="acknowledged"
NoAppliedLostReplyRecovery == ~(JoinedDone /\ FirstApplied /\ dropped /\ effect.wasAppliedBeforeReset)
NoUnknownAfterRestore == ~(JoinedDone /\ FirstId \in effect.unknown)
=============================================================================
