-------------------------- MODULE EarlyDispatch --------------------------
EXTENDS Contracts, Integers
CONSTANTS Events, Generations, Bug, OldSurvives, ReplacementCut
J == INSTANCE DurableLog
Owner == "dispatch-authority"
Authority == 0
Drivers == 1..Generations
EmptyRange == [events |-> {},driver |-> 0]
EmptyRanges == [g \in Drivers |-> EmptyRange]
JP == [owners |-> {Owner},actors |-> Drivers \cup {Authority},
 subscribers |-> [o \in {Owner} |-> {Authority}],initialConfig |-> [o \in {Owner} |-> 1]]
VARIABLES ranges, known, claims, pending, applied, submitted,
 generation,journal,network,cursor,seen,replaced,delayedOld,up,recovered,forgotten
vars == <<ranges,known,claims,pending,applied,submitted,generation,journal,network,cursor,seen,replaced,delayedOld,up,recovered,forgotten>>
Init == /\ ranges=EmptyRanges /\ known=[d \in Drivers |-> EmptyRanges]
 /\ claims=[d \in Drivers |-> {}] /\ pending={}
 /\ applied=[e \in 1..Events |-> 0] /\ submitted={} /\ generation=1
 /\ journal=J!Init(JP) /\ network={} /\ cursor=0 /\ seen={} /\ replaced=FALSE /\ delayedOld=FALSE
 /\ up=[d \in Drivers |-> d=1] /\ recovered={1} /\ forgotten=FALSE
Allocated == UNION {ranges[g].events:g \in Drivers}
Propose == \E e \in 1..Events:
 /\ ranges[generation].events={} /\ generation \in recovered
 /\ ToString(<<"range",generation>>) \notin submitted
 /\ (e \notin Allocated \/ Bug="fresh-generation-relabel")
 /\ LET c == Command(Owner,<<"range",generation>>,"early.range",[generation |-> generation,events |-> {e},driver |-> generation])
    IN /\ submitted'=submitted \cup {c.id}
       /\ network'=network \cup {Event(<<"submit",c.id>>,Authority,Owner,"journal.submit",c)}
 /\ UNCHANGED <<ranges,known,claims,pending,applied,generation,journal,cursor,seen,replaced,delayedOld,up,recovered,forgotten>>
Install(rs,c) == IF c.kind="early.range"
 THEN [rs EXCEPT ![c.body.generation]=[events |-> c.body.events,driver |-> c.body.driver]] ELSE rs
RECURSIVE Replay(_,_)
Replay(rs,cs) == IF cs= <<>> THEN rs ELSE Replay(Install(rs,Head(cs)),Tail(cs))
JournalStep == \E t \in J!Actions(JP,journal):
 /\ journal'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ UNCHANGED <<ranges,known,claims,pending,applied,submitted,generation,cursor,seen,replaced,delayedOld,up,recovered,forgotten>>
InputStep == \E e \in network:
 \/ /\ e.kind \in {"journal.submit","journal.recover"}
    /\ \E t \in J!Receive(JP,journal,e):
       /\ journal'=t.next /\ network'=(network \ {e}) \cup Elements(t.emissions)
       /\ UNCHANGED <<ranges,known,claims,pending,applied,submitted,generation,cursor,seen,replaced,delayedOld,up,recovered,forgotten>>
 \/ /\ e.kind="journal.deliver" /\ e.body.index=cursor+1
    /\ LET c == e.body.command
           fresh == c.kind="early.range" /\ c.id \notin seen
           valid == IF fresh THEN c.body.events \cap Allocated={} \/ Bug="fresh-generation-relabel" ELSE FALSE
       IN /\ ranges'=(IF valid THEN Install(ranges,c) ELSE ranges)
          /\ network'=(network \ {e}) \cup
            (IF valid THEN {Event(<<"grant",c.id>>,Authority,c.body.driver,"early.grant",c)} ELSE {})
    /\ seen'=seen \cup {e.body.command.id} /\ cursor'=e.body.index
    /\ UNCHANGED <<known,claims,pending,applied,submitted,generation,journal,replaced,delayedOld,up,recovered,forgotten>>
 \/ /\ e.kind="early.grant"
    /\ known'=(IF up[e.dst] THEN [known EXCEPT ![e.dst]=Install(@,e.body)] ELSE known)
    /\ network'=network \ {e}
    /\ UNCHANGED <<ranges,claims,pending,applied,submitted,generation,journal,cursor,seen,replaced,delayedOld,up,recovered,forgotten>>
 \/ /\ e.kind="journal.snapshot" /\ e.dst \notin recovered
    /\ known'=[known EXCEPT ![e.dst]=Replay(EmptyRanges,e.body.prefix)]
    /\ recovered'=recovered \cup {e.dst} /\ up'=[up EXCEPT ![e.dst]=TRUE]
    /\ network'=network \ {e}
    /\ UNCHANGED <<ranges,claims,pending,applied,submitted,generation,journal,cursor,seen,replaced,delayedOld,forgotten>>
\* Claims are actor-local volatile memory. Authority comes from the recovered
\* allocation's named driver, not from a global retired flag or another actor's
\* claim set. The same grant delivered/recovered at a new incarnation is inert.
Claim == \E d \in Drivers: \E g \in Drivers: \E e \in known[d][g].events \ claims[d]:
 /\ up[d] /\ d \in recovered
 /\ (known[d][g].driver=d \/ Bug="reuse-retired-range")
 /\ claims'=[claims EXCEPT ![d]=@ \cup {e}]
 /\ pending'=pending \cup {<<d,e>>}
 /\ UNCHANGED <<ranges,known,applied,submitted,generation,journal,network,cursor,seen,replaced,delayedOld,up,recovered,forgotten>>
\* These are already-issued external attempts. They may arrive after the
\* issuing process dies. The observer counts by original processor/event.
Dispatch == \E item \in pending:
 /\ delayedOld'=(delayedOld \/ (replaced /\ item[1]=1))
 /\ applied'=[applied EXCEPT ![item[2]]=@+1] /\ pending'=pending \ {item}
 /\ UNCHANGED <<ranges,known,claims,submitted,generation,journal,network,cursor,seen,replaced,up,recovered,forgotten>>
Replace == /\ ~replaced /\ generation<Generations /\ ranges[generation].events#{}
 /\ (ReplacementCut="any" \/ \E e \in ranges[generation].events:applied[e]>0)
 /\ generation'=generation+1 /\ replaced'=TRUE
 /\ claims'=(IF OldSurvives THEN claims ELSE [claims EXCEPT ![generation]={}])
 /\ known'=(IF OldSurvives THEN known ELSE [known EXCEPT ![generation]=EmptyRanges])
 /\ up'=(IF OldSurvives THEN up ELSE [up EXCEPT ![generation]=FALSE])
 /\ forgotten'=(~OldSurvives /\ claims[generation]#{})
 /\ network'=network \cup {Event(<<"recover",generation+1>>,generation+1,Owner,"journal.recover",[recovery |-> generation+1])}
 /\ UNCHANGED <<ranges,pending,applied,submitted,journal,cursor,seen,delayedOld,recovered>>
Done == /\ Allocated=1..Events /\ pending={}
 /\ \A d \in Drivers:up[d] => \A g \in Drivers:ranges[g].driver=d => ranges[g].events \subseteq claims[d]
Terminal == Done /\ UNCHANGED vars
Next == Propose \/ JournalStep \/ InputStep \/ Claim \/ Dispatch \/ Replace \/ Terminal
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Propose) /\ WF_vars(JournalStep) /\ WF_vars(InputStep)
 /\ WF_vars(Claim) /\ WF_vars(Dispatch) /\ WF_vars(Replace)
AtMostOnce == \A e \in 1..Events:applied[e]<=1
DisjointRanges == \A g,h \in Drivers:g#h => ranges[g].events \cap ranges[h].events={}
Completes == <>Done
NoEarlyUse == \A e \in 1..Events:applied[e]=0
NoDelayedOldUse == ~delayedOld
NoForgottenRecovery == ~(forgotten /\ 2 \in recovered /\ Done)
=============================================================================
