------------------------- MODULE RecoveryMaterial -------------------------
EXTENDS Contracts, Integers
CONSTANTS Reset, Collect, Abort, Bug, Service, Transfer, HolderCount
R == INSTANCE RecoveryKernel
J == INSTANCE DurableLog
M == INSTANCE MaterialCore
Data == [t \in {"base","code","patch"} |->
  CASE t="base" -> [kind |-> "base",interpretation |-> "v1",content |-> <<2,4>>]
    [] t="code" -> [kind |-> "code",interpretation |-> "v1",content |-> "replace-byte-v1"]
    [] OTHER -> [kind |-> "patch",interpretation |-> "v1",content |-> [index |-> 1,value |-> 9]]]
Recipe == [id |-> "recipe",base |-> "base",code |-> "code",patches |-> <<"patch">>,interpretation |-> "v1"]
Roots == IF Transfer THEN {"old","new"} ELSE {"old"}
Request(r) == [root |-> r,holders |-> 1..HolderCount,recipe |-> Recipe,cut |-> 3,
 context |-> "captured",generation |-> 1,requester |-> "reader",view |-> r,
 rights |-> [read |-> {1,2},write |-> {}],
 successor |-> IF r="old" /\ Transfer THEN "new" ELSE "",
 predecessor |-> IF r="new" THEN "old" ELSE "",predecessorOwner |-> "root-owner"]
P == [roots |-> Roots,holders |-> 1..HolderCount,requests |-> [r \in Roots |-> Request(r)],
 data |-> Data,initial |-> [h \in 1..HolderCount |-> DOMAIN Data],
 reset |-> Reset,gc |-> Collect,abort |-> Abort,bad |-> Bug,
 retries |-> 1,owner |-> "roots",actor |-> "root-owner"]
JP == [owners |-> {P.owner},actors |-> {P.actor},
       subscribers |-> [o \in {P.owner} |-> {P.actor}],
       initialConfig |-> [o \in {P.owner} |-> 1]]
VARIABLES state,journal,network,inputs,outputs,closes
vars == <<state,journal,network,inputs,outputs,closes>>
Init == /\ state=R!Init(P) /\ journal=J!Init(JP) /\ network={}
        /\ inputs={} /\ outputs={} /\ closes={}
\* The requester survives the root-owner restart. Its acquisition/close intent
\* is not an owner cache; it repeats the same identities in the new incarnation.
Acquire == \E r \in Roots:
 /\ <<r,state.resets>> \notin inputs /\ state.up
 /\ inputs'=inputs \cup {<<r,state.resets>>}
 /\ UNCHANGED <<state,journal,outputs,closes>>
 /\ network'=network \cup {Event(<<"acquire",r,state.resets>>,"reader",P.actor,"root.acquire",Request(r))}
Close == \E r \in Roots:
 /\ \E b \in outputs:b.kind="Material" /\ b.body.root=r
 /\ <<r,state.resets>> \notin closes /\ state.up
 /\ closes'=closes \cup {<<r,state.resets>>}
 /\ network'=network \cup {Event(<<"close",r,state.resets>>,"reader",P.actor,"root.close",[root |-> r])}
 /\ UNCHANGED <<state,journal,inputs,outputs>>
\* Holder acquisition families start with each declared physical replica;
\* cross-owner copying/representation change is checked by MaterialTransfer.
KernelStep == \E t \in {tr \in R!Actions(P,state):tr.tag#"copy-immutable-material"}:
 /\ state'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ UNCHANGED <<journal,inputs,outputs,closes>>
JournalStep == /\ Service="full"
               /\ \E t \in J!Actions(JP,journal):
                    /\ journal'=t.next /\ network'=network \cup Elements(t.emissions)
                    /\ UNCHANGED <<state,inputs,outputs,closes>>
InputStep == \E e \in network:
 \/ /\ Service="full" /\ e.kind \in {"journal.submit","journal.recover"}
    /\ \E t \in J!Receive(JP,journal,e):
         /\ journal'=t.next /\ network'=(network \ {e}) \cup Elements(t.emissions)
         /\ UNCHANGED <<state,inputs,outputs,closes>>
 \/ /\ Service="folded" /\ e.kind="journal.submit"
    /\ state'=R!Apply(P,state,e.body) /\ network'=network \ {e}
    /\ UNCHANGED <<journal,inputs,outputs,closes>>
 \/ /\ e.kind \in {"ViewGrant","Material","RootFailure"}
    /\ outputs'=outputs \cup {e} /\ network'=network \ {e}
    /\ UNCHANGED <<state,journal,inputs,closes>>
 \/ /\ e.kind \notin {"journal.submit","journal.recover","ViewGrant","Material","RootFailure"}
    /\ \E t \in R!Receive(P,state,e):
         /\ state'=t.next /\ network'=(network \ {e}) \cup Elements(t.emissions)
         /\ UNCHANGED <<journal,inputs,outputs,closes>>
Done == \A r \in Roots:state.roots[r].phase \in {"released","aborted"}
Retired == Done /\ \A h \in P.holders:
 /\ \A r \in Roots:state.holds[h][r].phase="terminal"
 /\ R!V!RegistryDebt(state.registry[h])={}
Terminal == Retired /\ UNCHANGED vars
Next == Acquire \/ Close \/ KernelStep \/ JournalStep \/ InputStep \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Acquire) /\ WF_vars(Close) /\ WF_vars(KernelStep)
                 /\ WF_vars(JournalStep) /\ WF_vars(InputStep)
HeldExists == R!HeldExists(P,state)
LiveRetained == R!LiveRetained(P,state)
ExactBytes == R!ExactBytes(P,state)
NoResurrection == R!NoResurrection(P,state)
Completes == <>Retired
NoGrant == state.granted={}
NoRetirement == ~Retired
NoLateWrite == ~state.lateWrites
NoRecovery == ~(state.resets>0 /\ state.up /\ Done)
=============================================================================
