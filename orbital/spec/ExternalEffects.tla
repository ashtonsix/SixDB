--------------------------- MODULE ExternalEffects ---------------------------
EXTENDS Contracts, TxFixtures, Integers
CONSTANTS TxNone, Dedup, Reset, LoseReply, Bug
T == INSTANCE TxKernel
E == INSTANCE EffectKernel
J == INSTANCE DurableLog
P == Params("single","none",FALSE)
EP == [owner |-> "effects",actor |-> "effect-driver",dedup |-> Dedup,reset |-> Reset,restore |-> FALSE,bad |-> Bug]
JP == [owners |-> {EP.owner},actors |-> {EP.actor},
 subscribers |-> [o \in {EP.owner} |-> {EP.actor}],initialConfig |-> [o \in {EP.owner} |-> 1]]
VARIABLES tx,effect,journal,network,dropped
vars == <<tx,effect,journal,network,dropped>>
Init == /\ tx=T!Init(P) /\ effect=E!Init(EP) /\ journal=J!Init(JP) /\ network={} /\ dropped=FALSE
RECURSIVE Facts(_,_)
Facts(st,es) == IF es= <<>> THEN st ELSE
 IF Head(es).kind="tx.fact"
 THEN LET ts == T!Receive(P,st,Head(es))
      IN Facts(IF ts={} THEN st ELSE (CHOOSE t \in ts:TRUE).next,Tail(es))
 ELSE Facts(st,Tail(es))
Forward(es) == {e \in Elements(es):e.kind#"tx.fact"}
TxStep == \E t \in T!Actions(P,tx):
 /\ tx'=Facts(t.next,t.emissions) /\ network'=network \cup Forward(t.emissions)
 /\ UNCHANGED <<effect,journal,dropped>>
EffectStep == \E t \in E!Actions(EP,effect):
 /\ effect'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ UNCHANGED <<tx,journal,dropped>>
JournalStep == \E t \in J!Actions(JP,journal):
 /\ journal'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ UNCHANGED <<tx,effect,dropped>>
InputStep == \E e \in network:
 \/ /\ e.kind="journal.submit" /\ ToString(e.body.owner) \in {ToString(a):a \in P.shards}
    /\ \E t \in T!CommitAndFold(P,tx,e.body):
       /\ tx'=Facts(t.next,t.emissions) /\ network'=(network \ {e}) \cup Forward(t.emissions)
       /\ UNCHANGED <<effect,journal,dropped>>
 \/ /\ e.kind \in {"journal.submit","journal.recover"} /\ ToString(e.dst)=ToString(EP.owner)
    /\ \E t \in J!Receive(JP,journal,e):
       /\ journal'=t.next /\ network'=(network \ {e}) \cup Elements(t.emissions)
       /\ UNCHANGED <<tx,effect,dropped>>
 \/ /\ e.kind \notin {"journal.submit","journal.recover"}
    /\ \E t \in E!Receive(EP,effect,e):
       /\ effect'=t.next /\ network'=(network \ {e}) \cup Elements(t.emissions)
       /\ UNCHANGED <<tx,journal,dropped>>
DropReply == \E e \in network:
 \* One authored failure episode: a reply can be lost before the one reset.
 \* The replacement retries once because its driver incarnation is new.
 /\ LoseReply /\ ~dropped /\ ~effect.reset /\ e.kind="effect.reply"
 /\ network'=network \ {e} /\ dropped'=TRUE /\ UNCHANGED <<tx,effect,journal>>
Done == tx.published=P.transactions /\ E!Resolved(EP,effect)
Terminal == Done /\ UNCHANGED vars
Next == TxStep \/ EffectStep \/ JournalStep \/ InputStep \/ DropReply \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(TxStep) /\ WF_vars(EffectStep) /\ WF_vars(JournalStep) /\ WF_vars(InputStep)
AtMostOnce == E!AtMostOnce(EP,effect)
CommittedOnly == E!CommittedOnly(EP,effect) /\
 (DOMAIN effect.applications#{} => tx.decision[1]="commit" /\ 1 \in tx.published)
KnownResults == \A id \in DOMAIN effect.intents:
 effect.intents[id].phase="acknowledged" => id \in DOMAIN effect.applications /\ effect.applications[id]=1
Completes == <>Done
NoAppliedLostReplyRecovery == ~(dropped /\ effect.reset /\ effect.wasAppliedBeforeReset /\ Done)
NoUnknown == effect.unknown={}
=============================================================================
