------------------------- MODULE DeliveryUnderPressure -------------------------
EXTENDS Contracts, Integers
CONSTANTS Mode, Bug
D == INSTANCE DeliveryKernel
J == INSTANCE DurableLog
C == INSTANCE CapacityKernel
V == INSTANCE ViewsKernel
P == [keys|->IF Mode="child" THEN {1,2} ELSE {1},baseKeys|->{1},child|->2,parent|->1,
 origins|->{1},recipients|->{"1","2"},initialMembers|->IF Mode="join" THEN {"1"} ELSE {"1","2"},
 inputs|->[o \in {1}|->[k \in {1}|->3]],lineage|->"capacity",call|->"map",cut|->4,interpretation|->"v1",
 forms|->{"raw"},routes|->1,duplicates|->IF Mode="duplicate" THEN 2 ELSE 1,dynamic|->Mode="child",
 cancelRecipients|->{"1"},reset|->Mode="reset",cancel|->Mode="cancel",join|->Mode="join",bad|->"none",owner|->"producer"]
Owners == {P.owner} \cup P.recipients
JP == [owners|->Owners,actors|->Owners,subscribers|->[o \in Owners|->{o}],initialConfig|->[o \in Owners|->1]]
Resources == {"ordinary-memory","ordinary-worker","worker","buffer","send","io","metadata"} \cup Owners
Zero == [r \in Resources|->0]
Caps == [r \in Resources|->CASE r \in {"ordinary-memory","ordinary-worker","worker"} -> IF r="worker" /\ Bug="no-worker" THEN 0 ELSE 1
 [] r \in Owners -> IF Bug="short-recipient" /\ r="2" THEN 1 ELSE 12
 [] r="metadata" -> 36 [] OTHER ->4]
Ordinary == [Zero EXCEPT !["ordinary-memory"]=1,!["ordinary-worker"]=1]
Worker == [Zero EXCEPT !["worker"]=1]
Message == [Zero EXCEPT !["buffer"]=1,!["send"]=1,!["io"]=1]
Record(c) == [Zero EXCEPT ![c.owner]=1,!["metadata"]=1]
RecordId(c) == ToString(<<"record",c.owner,c.id>>)
PoolRelease(pool,id) == IF id \in DOMAIN pool.leases THEN (CHOOSE tr \in C!Release(pool,id):TRUE).next ELSE pool
RECURSIVE ChargeRecords(_,_)
ChargeRecords(pool,cs) == IF cs={} THEN {pool} ELSE
 LET c==CHOOSE c \in cs:TRUE id==RecordId(c)
     choices==IF id \in DOMAIN pool.leases THEN {pool}
              ELSE {tr.next:tr \in C!Request(Caps,pool,C!Lease(id,c.owner,"retained-command",Record(c)))}
 IN UNION {ChargeRecords(q,cs \ {c}):q \in choices}
VARIABLE run
vars == <<run>>
Init == run=[state|->D!Init(P),journal|->J!Init(JP),backend|->V!RegistryInit,
 pool|->[C!PoolInit EXCEPT !.leases=("ordinary" :> C!Lease("ordinary","busy","ordinary",Ordinary))],
 serial|->0,ready|->{},consumed|->{},started|->FALSE,tick|->FALSE,logicalCancel|->FALSE]
CommandKind(tr) == IF tr.emissions= <<>> THEN "" ELSE
 IF Head(tr.emissions).kind="journal.submit" THEN Head(tr.emissions).body.kind ELSE ""
Allowed(tr) ==
 /\ (CommandKind(tr)#"delivery.cancel" \/ DOMAIN run.state.custody["1"]#{})
 /\ (CommandKind(tr)#"delivery.expand" \/ run.state.outbox[1].coverage \subseteq run.state.done[1] \cup run.state.cancelled)
 /\ (CommandKind(tr)#"delivery.join" \/ 1 \in run.state.closed)
 /\ (tr.tag#"recipient-process-reset" \/
      (tr.next.restarted \ run.state.restarted={"1"} /\ DOMAIN run.state.custody["1"]#{}))
 /\ (CommandKind(tr)#"delivery.complete" \/ Mode#"reset" \/
      Head(tr.emissions).body.owner#"1" \/ "1" \in run.state.restarted)
\* Emissions register real backend sends. Delivery and physical retirement are
\* distinct uses of the buffer. Command leases survive both and count the log.
Lift(nextState,nextJournal,emissions,pool,consumed,cancel) ==
 LET cs=={e.body:e \in {x \in Elements(emissions):x.kind="journal.submit"}} \cup
         {e.body:e \in nextJournal.pending \ run.journal.pending}
     pools==ChargeRecords(pool,cs)
 IN IF emissions= <<>> THEN
 {Transition("local",[run EXCEPT !.state=nextState,!.journal=nextJournal,!.pool=q,!.consumed=consumed,!.logicalCancel=cancel],<<>>):q \in pools}
 ELSE IF Len(emissions)=1 THEN
 LET e==Head(emissions) id==ToString(<<"send",run.serial+1>>)
     request==[id|->id,generation|->run.backend.generation,target|->e.dst,kind|->"send",payload|->e]
 IN UNION {{Transition("send",[run EXCEPT !.state=nextState,!.journal=nextJournal,
       !.pool=a.next,!.backend=b.next,!.serial=@+1,!.consumed=consumed,!.logicalCancel=cancel],<<>>):
      a \in C!Request(Caps,q,C!Lease(id,e.src,"physical-send",Message)),b \in V!RegistryRegister(run.backend,request)}:q \in pools}
 ELSE {}
KernelChoices == UNION {Lift(tr.next,run.journal,tr.emissions,run.pool,run.consumed,run.logicalCancel):tr \in {x \in D!Actions(P,run.state):Allowed(x)}}
JournalChoices == UNION {Lift(run.state,tr.next,tr.emissions,run.pool,run.consumed,run.logicalCancel):tr \in J!Actions(JP,run.journal)}
Input(id) ==
 LET op==run.backend.operations[id] e==op.request.payload
     pool==IF op.phase="retired" THEN PoolRelease(run.pool,id) ELSE run.pool
     cancel==run.logicalCancel \/ e.kind="delivery.cancelled"
     actualPool==IF Bug="cancel-debt" /\ e.kind="delivery.cancelled"
                 THEN [pool EXCEPT !.leases=[i \in {x \in DOMAIN @:@[x].kind#"physical-send"}|->@[i]]]
                 ELSE pool
 IN IF e.kind \in {"journal.submit","journal.recover"}
 THEN UNION {Lift(run.state,tr.next,tr.emissions,actualPool,run.consumed \cup {id},cancel):tr \in J!Receive(JP,run.journal,e)}
 ELSE UNION {Lift(tr.next,run.journal,tr.emissions,actualPool,run.consumed \cup {id},cancel):tr \in D!Receive(P,run.state,e)}
InputChoices == UNION {Input(id):id \in run.ready \ run.consumed}
Start == \E tr \in C!Request(Caps,run.pool,C!Lease("control","delivery","reserved-service",Worker)):
 /\ ~run.started /\ run'=[run EXCEPT !.started=TRUE,!.pool=tr.next]
Service ==
 /\ run.started
 /\ LET choices==IF InputChoices#{} THEN InputChoices ELSE IF JournalChoices#{} THEN JournalChoices ELSE KernelChoices
        tr==CHOOSE tr \in choices:TRUE
    IN /\ choices#{} /\ run'=tr.next
PhysicalChoices ==
 {tr \in V!RegistryActions([actor|->"network",owner|->"delivery"],run.backend):
  IF Mode="cancel" /\ ~run.logicalCancel /\ tr.emissions# <<>> /\ Head(tr.emissions).kind="BackendRetired"
  THEN LET req==Head(tr.emissions).body IN ~(req.payload.kind="delivery.payload" /\ req.target="1") ELSE TRUE}
Physical ==
 /\ run.started /\ PhysicalChoices#{}
 /\ LET tr==CHOOSE tr \in PhysicalChoices:TRUE IN
    LET complete=={e.body.id:e \in {x \in Elements(tr.emissions):x.kind="BackendCompleted"}}
        retired=={e.body.id:e \in {x \in Elements(tr.emissions):x.kind="BackendRetired"}}
        id==IF retired={} THEN "" ELSE CHOOSE i \in retired:TRUE
        op==IF id="" THEN [payload|->[kind|->"none"],target|->""] ELSE run.backend.operations[id].request
        hold==Mode="cancel" /\ ~run.logicalCancel /\ op.payload.kind="delivery.payload" /\ op.target="1"
    IN /\ ~hold
       /\ run'=[run EXCEPT !.backend=tr.next,!.ready=@ \cup complete,
          !.pool=IF id \in run.consumed THEN PoolRelease(@,id) ELSE @]
Tick == run'=[run EXCEPT !.tick=~@]
Done == /\ P.keys \subseteq run.state.closed
 /\ (Mode#"join" \/ "2" \in run.state.joinedComplete)
 /\ (Mode#"cancel" \/ run.logicalCancel)
 /\ (Mode#"reset" \/ "1" \in run.state.restarted)
 /\ run.ready \subseteq run.consumed /\ V!RegistryDebt(run.backend)={}
Next == (IF ENABLED Start THEN Start ELSE IF ENABLED Service THEN Service ELSE Physical) \/ Tick
Spec == Init /\ [][Next]_vars /\ WF_vars(Start) /\ WF_vars(Service) /\ WF_vars(Physical) /\ WF_vars(Tick)
Completes == <>Done
ResourcesBounded == \A r \in Resources:C!Usage(run.pool,r)<=Caps[r]
PhysicalLifetime ==
 /\ V!RegistryDebt(run.backend) \subseteq DOMAIN run.pool.leases
 /\ run.ready \ run.consumed \subseteq DOMAIN run.pool.leases
RecordedCommands ==
 \A c \in UNION {Elements(run.journal.log[o]):o \in Owners} \cup {e.body:e \in run.journal.pending}:
 RecordId(c) \in DOMAIN run.pool.leases /\ run.pool.leases[RecordId(c)].demand=Record(c)
EffectMultiplicity == D!EffectMultiplicity(P,run.state)
LogicalCompletion == D!Coverage(P,run.state) /\ D!JoinedCoverage(P,run.state) /\ D!CancellationAuthority(P,run.state)
ActualCheckpoints == D!Checkpoints(P,run.state)
SingleEmission ==
 /\ \A tr \in D!Actions(P,run.state) \cup J!Actions(JP,run.journal):Len(tr.emissions)<=1
 /\ \A id \in run.ready \ run.consumed:
   LET e==run.backend.operations[id].request.payload
       choices==IF e.kind \in {"journal.submit","journal.recover"} THEN J!Receive(JP,run.journal,e) ELSE D!Receive(P,run.state,e)
   IN \A tr \in choices:Len(tr.emissions)<=1
DistinctLogEntries == \A o \in Owners:Cardinality({c.id:c \in Elements(run.journal.log[o])})=Len(run.journal.log[o])
NoComplete == ~Done
NoCancelledDebt == ~(run.logicalCancel /\ V!RegistryDebt(run.backend)#{})
NoPhysicalAfterLogical == ~(P.keys \subseteq run.state.closed /\ V!RegistryDebt(run.backend)#{})
=============================================================================
