------------------------- MODULE RetentionRaceProbe -------------------------
EXTENDS Contracts, Integers, TxFixtures
CONSTANTS Scenario, Bug, TxNone
K == INSTANCE TxKernel
R == INSTANCE CutMaterialKernel
O == INSTANCE TransactionOracle

\* Named causal histories, not a reduction of the unrestricted T/R product.
\* Protocol transitions and folded commands are shared. Driver facts and the
\* same-owner R attachment are delivered in their emitting step. Physical
\* persistence, GC, protection replies and recipe replies remain interleaved.
Chain == Scenario="chain"
Base == Params(IF Chain THEN "chain" ELSE "invalidation",
               IF Bug="last-arrival" THEN Bug ELSE "none",FALSE)
P == IF Chain THEN [Base EXCEPT !.material=TRUE]
 ELSE [Base EXCEPT !.material=TRUE,
       !.writes=[t \in Base.transactions |-> CASE t=1 -> {2} [] t=2 -> {1} [] OTHER -> {}],
       !.reads=[t \in Base.transactions |-> IF t=3 THEN {2} ELSE {}],
       !.parts=[t \in Base.transactions |-> IF t=3 THEN {} ELSE {2}],
       !.program=[t \in Base.transactions |-> CASE t=1 -> "put" [] t=2 -> "none" [] OTHER -> "read"],
       !.firstRead=[t \in Base.transactions |-> 2],
       !.value=[t \in Base.transactions |-> IF t=1 THEN 9 ELSE 1],
       !.causal=[t \in Base.transactions |-> IF t=1 THEN 8 ELSE 0],!.slow={2}]
RP == [shards |-> P.shards,transactions |-> P.transactions,keys |-> P.keys,
       home |-> P.home,initial |-> [k \in P.keys |-> 0],gc |-> TRUE,bad |-> Bug]
Reader == IF Chain THEN 2 ELSE 3
ReadKey == IF Chain THEN 1 ELSE 2
PendingTx == IF Chain THEN 1 ELSE 2
Replacement == IF Chain THEN 3 ELSE 1
ReadRoot == K!Root(Reader,ReadKey)
RootId == ToString(ReadRoot)
FallbackToken == R!Token(ReadKey,0)
ReplacementToken(s) == R!Token(ReadKey,s.position[Replacement])
LateToken(s) == R!Token(1,s.position[2])

VARIABLES state,material,network,phase,evidence
vars == <<state,material,network,phase,evidence>>
Init == /\ state=K!Init(P) /\ material=R!Init(RP) /\ network={}
        /\ phase=0 /\ evidence={}

\* Observer facts identify one fixed root throughout; no protocol guard reads
\* this evidence. A milestone cannot be supplied by another reader's closure.
Observed(m,t,k,v) == \E o \in m.observations:
 o.request.root=K!Root(t,k) /\ o.request.tx=t /\ o.request.key=k /\ o.bytes= <<v>>
SourcePending(s) == s.ticket[PendingTx][P.home[ReadKey]]="fixed"
Evidence(s,m) ==
 (IF RootId \in DOMAIN m.closures /\ SourcePending(s) /\
       ToString(<<P.home[ReadKey],PendingTx>>) \in m.closures[RootId].pending
  THEN {"captured-pending"} ELSE {}) \cup
 (IF Chain /\ RootId \in DOMAIN m.closures /\ SourcePending(s) /\
       s.position[1]<s.readCut[2][1] /\ s.readCut[2][1]<s.position[3] /\
       FallbackToken \in m.closures[RootId].fallback /\
       FallbackToken \in DOMAIN m.data /\ FallbackToken \notin R!Heads(RP,m) /\
       ReplacementToken(s) \in DOMAIN m.data
  THEN {"fallback-under-pressure"} ELSE {}) \cup
 (IF Chain /\ SourcePending(s) /\ Observed(m,4,1,9) /\ s.inputs[4][1]=9
  THEN {"replacement-read-before-resolution"} ELSE {}) \cup
 (IF Chain /\ ~SourcePending(s) /\ s.decision[1]="commit" /\
       s.outcome[1]#TxNone /\ DOMAIN s.outcome[1].effects={} /\
       Observed(m,2,1,0) /\ s.inputs[2][1]=0
  THEN {"same-root-fallback-consumed"} ELSE {}) \cup
 (IF ~Chain /\ RootId \in DOMAIN m.closures /\ SourcePending(s) /\
       s.position[2]<s.position[1] /\ s.position[1]<=s.readCut[3][2] /\
       ReplacementToken(s) \in DOMAIN m.data /\ RootId \notin m.ready
  THEN {"cross-key-pending"} ELSE {}) \cup
 (IF ~Chain /\ ~SourcePending(s) /\ s.decision[2]="commit" /\
       s.outcome[2]#TxNone /\ DOMAIN s.outcome[2].effects={} /\
       Observed(m,3,2,9) /\ s.inputs[3][2]=9
  THEN {"same-root-after-invalidator"} ELSE {})

RECURSIVE Facts(_,_)
Facts(s,es) == IF es= <<>> THEN s ELSE
 IF Head(es).kind="tx.fact"
 THEN LET ts==K!Receive(P,s,Head(es))
      IN Facts(IF ts={} THEN s ELSE (CHOOSE t \in ts:TRUE).next,Tail(es))
 ELSE Facts(s,Tail(es))
Forward(es) == {e \in Elements(es):e.kind \notin {"tx.fact","root.source","root.retain-cut"}}
Take(t,remaining) ==
 LET ns==Facts(t.next,t.emissions)
     nm==R!ObserveAll(RP,material,t.emissions)
 IN /\ state'=ns /\ material'=nm /\ network'=remaining \cup Forward(t.emissions)
    /\ evidence'=evidence \cup Evidence(ns,nm) /\ UNCHANGED phase

\* Chain: pending no-effect -> captured RMW -> newer replacement -> later
\* reader observes replacement -> no-effect resolution -> old RMW installs.
\* Cross-key: pending key1 source -> newer key2 replacement -> key2 bound
\* covering {1,2} -> release key1 source -> same bound reads key2 replacement.
Active == IF Chain THEN CASE phase=0 -> 1 [] phase=1 -> 2 [] phase=2 -> 3
 [] phase=3 -> 4 [] phase=4 -> 1 [] OTHER -> 2
 ELSE CASE phase=0 -> 2 [] phase=1 -> 1 [] phase=2 -> 3 [] phase=3 -> 2 [] OTHER -> 3
EndPhase == IF Chain THEN 6 ELSE 5
Reached == IF Chain THEN CASE
 phase=0 -> state.fixed[1][1]>0
 [] phase=1 -> 1 \in state.registered[2]
 [] phase=2 -> 3 \in state.published /\ ReplacementToken(state) \in DOMAIN material.data
 [] phase=3 -> 4 \in state.published
 [] phase=4 -> 1 \in state.published
 [] phase=5 -> 2 \in state.published /\ LateToken(state) \in DOMAIN material.data
 [] OTHER -> FALSE
 ELSE CASE phase=0 -> state.fixed[2][2]>0
 [] phase=1 -> 1 \in state.published /\ ReplacementToken(state) \in DOMAIN material.data
 [] phase=2 -> 2 \in state.registered[3]
 [] phase=3 -> 2 \in state.published
 [] phase=4 -> 3 \in state.published
 [] OTHER -> FALSE
Advance == /\ phase<EndPhase /\ Reached /\ phase'=phase+1
           /\ UNCHANGED <<state,material,network,evidence>>
Candidates == K!SubmitActions(P,state,Active) \cup K!ReadActions(P,state,Active) \cup
 K!ComputeActions(P,state,Active) \cup K!PublishActions(P,state,Active) \cup
 {K!Grant(P,state,Active,a):a \in {x \in P.shards:K!CanGrant(P,state,Active,x)}}
KernelStep == /\ phase<EndPhase
 /\ \E t \in {x \in K!Actions(P,state):x \in Candidates \/
       (x.tag="tx.source-arrives" /\ phase=(IF Chain THEN 4 ELSE 3))}:Take(t,network)
FoldStep == \E e \in {x \in network:x.kind="journal.submit"}:
 \E t \in K!CommitAndFold(P,state,e.body):Take(t,network \ {e})
InputStep == \E e \in network:
 \/ /\ e.kind \in {"root.cut-protected","root.recipe-ready","root.unavailable"}
    /\ \E t \in K!Receive(P,state,e):Take(t,network \ {e})
 \/ /\ e.kind="tx.published" /\ network'=network \ {e}
    /\ UNCHANGED <<state,material,phase,evidence>>

\* The key-only control deliberately narrows only the provider's readiness
\* request. It models dropping dependency scope at that interface, while the
\* real registration, stored root, T invalidation and observers stay intact.
MaterialActions == IF Bug#"key-only-shadow" THEN R!Actions(RP,material)
 ELSE UNION {R!Materialize(RP,material,v):v \in material.pending} \cup
      UNION {R!Protection(RP,material,b) \cup R!Unavailable(RP,material,b) \cup
             R!Ready(RP,material,[b EXCEPT !.scopes={b.key}]):b \in material.requests} \cup
      UNION {R!Collect(RP,material,id):id \in DOMAIN material.data}
MaterialStep == \E t \in MaterialActions:
 /\ material'=t.next /\ network'=network \cup Elements(t.emissions)
 /\ evidence'=evidence \cup Evidence(state,t.next) /\ UNCHANGED <<state,phase>>
LogicalService == IF Reached THEN Advance ELSE IF ENABLED FoldStep THEN FoldStep
 ELSE IF ENABLED InputStep THEN InputStep ELSE KernelStep
Done == phase=EndPhase
Next == LogicalService \/ MaterialStep \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(LogicalService) /\ WF_vars(MaterialStep)
Completes == <>Done

ExactMaterial == R!Exact(RP,material)
RetainedCoverage == R!CoverageRetained(RP,material)
SerialReads == O!ObservedAtPosition(P,state)
SerialOutcomes == O!ResultSemantics(P,state)
JustifiedOutcome == O!OutcomeJustified(P,state)
Publication == O!PublishedSemantics(P,state)
ReplacementSurvives == (Chain /\ phase>=3) =>
 \E v \in state.versions:v.tx=3 /\ v.key=1 /\ v.position=state.position[3] /\ v.value=9
CrossKeyBlocked == (~Chain /\ SourcePending(state) /\ RootId \in DOMAIN material.closures) =>
 /\ RootId \notin material.ready
 /\ ~(\E o \in material.observations:o.request.root=ReadRoot)
 /\ state.inputs[3][2]=TxNone
ChainEvidence == {"captured-pending","fallback-under-pressure",
 "replacement-read-before-resolution","same-root-fallback-consumed"} \subseteq evidence
InvalidationEvidence == {"captured-pending","cross-key-pending","same-root-after-invalidator"} \subseteq evidence
CausalCompletion == Done =>
 IF Chain THEN /\ ChainEvidence /\ state.outcome[2].effects[1]=1
               /\ LateToken(state) \in DOMAIN material.data
               /\ ReplacementToken(state) \in R!Heads(RP,material)
 ELSE InvalidationEvidence
NoCausalHistory == ~(Done /\ (IF Chain THEN ChainEvidence ELSE InvalidationEvidence))
=============================================================================
