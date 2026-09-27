-------------------------- MODULE CapacityKernel --------------------------
EXTENDS Naturals, Integers, Sequences, FiniteSets, TLC, Contracts

\* Pools own named live leases, rather than independent increment/decrement
\* counters. A composed provider creates a lease before creating its physical
\* object and releases it only from the object's real retirement transition.
PoolInit == [leases |-> [id \in {} |-> id], retired |-> {}]
\* Set summation without a mutable accounting counter. The set contains leases,
\* so equal-sized allocations still contribute separately.
RECURSIVE Total(_,_,_)
Total(leases, ids, resource) ==
    IF ids = {} THEN 0
    ELSE LET id == CHOOSE x \in ids : TRUE
         IN leases[id].demand[resource] + Total(leases, ids \ {id}, resource)
Usage(pool, resource) == Total(pool.leases, DOMAIN pool.leases, resource)
Request(capacities, pool, lease) ==
    IF DOMAIN lease.demand = DOMAIN capacities /\
       (\A r \in DOMAIN capacities : lease.demand[r] \in Nat) /\
       lease.id \notin DOMAIN pool.leases /\ lease.id \notin pool.retired /\
       \A r \in DOMAIN capacities :
          Usage(pool,r) + lease.demand[r] <= capacities[r]
    THEN {Transition("allocate",
            [pool EXCEPT !.leases = @ @@ (lease.id :> lease)], <<>>)}
    ELSE {}
Release(pool, id) ==
    IF id \in DOMAIN pool.leases
    THEN {Transition("retire",
            [pool EXCEPT !.leases =
                 [key \in DOMAIN @ \ {id} |-> @[key]], !.retired = @ \cup {id}], <<>>)}
    ELSE {}
Resize(capacities,pool,id,demand) ==
    IF id \in DOMAIN pool.leases /\ DOMAIN demand = DOMAIN capacities /\
       (\A r \in DOMAIN capacities : demand[r] \in Nat) /\
       \A r \in DOMAIN capacities :
         Usage(pool,r) - pool.leases[id].demand[r] + demand[r] <= capacities[r]
    THEN {Transition("resize",
            [pool EXCEPT !.leases[id].demand = demand], <<>>)}
    ELSE {}

Resources == {"working","fault-memory","fault-worker","metadata","io","send","probe",
              "record1","record2","record3"}
ZeroDemand == [r \in Resources |-> 0]
RecordResource(w) == CASE w=1 -> "record1" [] w=2 -> "record2" [] OTHER -> "record3"
Bundle ==
    [r \in Resources |-> IF r \in {"record1","record2","record3"} THEN 2
                         ELSE IF r \in {"working","metadata","send"} THEN 1 ELSE 0]
FaultDemand(p) ==
    [r \in Resources |-> IF r = "fault-memory"
                         THEN IF p.mutant = "no-fault-memory" THEN 0 ELSE 1
                         ELSE IF r \in {"fault-worker","io"} THEN 1 ELSE 0]
Lease(id,owner,kind,demand) == [id |-> id,owner |-> owner,kind |-> kind,demand |-> demand]
Phases == {"outcome","decision"}
EmptyTask ==
    [phase |-> "offered", worker |-> FALSE, heldLatch |-> FALSE,
     observed |-> 0, result |-> 0, reason |-> "", outcomeAcks |-> {},
     decisionAcks |-> {}, cancelled |-> FALSE, replied |-> FALSE]
EmptyRecord == [phase |-> "none", bytes |-> 0, acked |-> FALSE]
Init(p) ==
    [pool |-> PoolInit, tasks |-> [t \in p.tasks |-> EmptyTask],
     records |-> [w \in 1..3 |-> [t \in p.tasks |->
                   [kind \in Phases |-> EmptyRecord]]],
     fault |-> [task |-> "", phase |-> "idle", bytes |-> 0],
     source |-> p.source,
     sourceBytes |-> [t \in p.tasks |-> IF p.dependent /\ t="task2" THEN 0 ELSE p.source],
     availableSources |-> IF p.dependent THEN {"task1"} ELSE p.tasks,
     ordinaryFilled |-> FALSE,
     filler |-> [w \in 1..3 |-> 0], seen |-> {},
     physicalReads |-> {}, writes |-> {}, reported |-> {},
     subscribers |-> {"required","optional"}, cancelledOptional |-> FALSE,
     shared |-> "idle", sharedCharged |-> FALSE,
     alarmSeen |-> FALSE, probes |-> {}, probeRetired |-> {},
     restarted |-> FALSE, incarnation |-> 1]
OrdinaryWorkers(s) == Cardinality({t \in DOMAIN s.tasks : s.tasks[t].worker})
AllAdmitted(s) == \A t \in DOMAIN s.tasks : s.tasks[t].phase # "offered"
AnyDone(s) == \E t \in DOMAIN s.tasks : s.tasks[t].phase = "done"
RecordsAt(s,w) ==
    Cardinality({<<t,k>> \in (DOMAIN s.tasks) \X Phases :
                 s.records[w][t][k].phase # "none"})
PersistedAt(s,w) ==
    {<<t,k>> \in (DOMAIN s.tasks) \X Phases :
        s.records[w][t][k].phase = "durable"}
EventFor(p,id,src,dst,kind,body) ==
    Event(<<p.actor,id,kind>>,src,dst,kind,body)
Witness(w) == CASE w=1 -> "witness1" [] w=2 -> "witness2" [] OTHER -> "witness3"
BundleId(t) == <<"bundle",t>>
FaultId(t) == <<"fault",t>>

Admit(p,s,t) ==
    IF s.tasks[t].phase = "offered"
    THEN {Transition("admit",
            [s EXCEPT !.pool = step.next, !.tasks[t].phase = "admitted"], <<>>)
          : step \in Request(p.capacities,s.pool,
              Lease(BundleId(t),t,"completion-bundle",Bundle))}
    ELSE {}
Start(p,s,t) ==
    IF s.tasks[t].phase = "admitted" /\ OrdinaryWorkers(s) < p.workers /\
       ~(\E q \in p.tasks : s.tasks[q].heldLatch)
    THEN {Transition("start",
            [s EXCEPT !.tasks[t].phase = "running", !.tasks[t].worker = TRUE,
                      !.tasks[t].heldLatch = TRUE], <<>>)}
    ELSE {}
Touch(p,s,t) ==
    IF s.tasks[t].phase = "running"
    THEN {Transition("first-touch",
            [s EXCEPT !.tasks[t].phase = "faulted",
                      !.tasks[t].heldLatch = p.mutant = "held-latch"], <<>>)}
    ELSE {}
FaultStart(p,s,t) ==
    LET runnable == p.mutant # "no-resolver-worker" \/ OrdinaryWorkers(s) < p.workers
        bytesAvailable == p.mutant # "no-fault-memory" \/
                          Usage(s.pool,"working") < p.capacities["working"]
    IN IF s.tasks[t].phase = "faulted" /\ s.fault.phase = "idle" /\
          t \in s.availableSources /\
          runnable /\ bytesAvailable /\
          ~(\E q \in p.tasks : s.tasks[q].heldLatch)
       THEN {Transition("fault-start",
               [s EXCEPT !.pool = step.next,
                         !.fault = [task |-> t,phase |-> "queued",bytes |-> 0]], <<>>)
             : step \in Request(p.capacities,s.pool,
                 Lease(FaultId(t),t,"fault-service",FaultDemand(p)))}
       ELSE {}
FaultRead(p,s) ==
    IF s.fault.phase = "queued"
    THEN {Transition("fault-read",
           [s EXCEPT !.fault.phase = "complete", !.fault.bytes = s.sourceBytes[s.fault.task],
                     !.physicalReads = @ \cup {[task |-> s.fault.task, bytes |-> s.sourceBytes[s.fault.task]]}],
           <<EventFor(p,s.fault.task,"storage","runtime","FaultBytes",
                      [task |-> s.fault.task,bytes |-> s.sourceBytes[s.fault.task]])>>)}
    ELSE {}
FaultWake(p,s) ==
    LET t == s.fault.task
    IN IF s.fault.phase = "complete"
       THEN {Transition("fault-wake",
               [s EXCEPT !.pool = step.next, !.tasks[t].phase = "ready",
                         !.tasks[t].observed = s.fault.bytes,
                         !.fault = [task |-> "",phase |-> "idle",bytes |-> 0]], <<>>)
             : step \in Release(s.pool,FaultId(t))}
       ELSE {}
Execute(p,s,t) ==
    IF s.tasks[t].phase = "ready"
    THEN {Transition("execute",
            [s EXCEPT !.tasks[t].phase = "computed", !.tasks[t].worker = FALSE,
                      !.tasks[t].result =
                         IF s.tasks[t].cancelled THEN -1 ELSE s.tasks[t].observed + 1,
                      !.tasks[t].reason =
                         IF s.tasks[t].cancelled THEN "cancel" ELSE "success"], <<>>)}
    ELSE {}
Cancel(p,s,t) ==
    IF p.cancel /\ ~s.tasks[t].cancelled /\
       s.tasks[t].phase \in {"admitted","running","faulted","ready"}
    THEN {Transition("cancel-request",
            [s EXCEPT !.tasks[t].cancelled = TRUE], <<>>)}
    ELSE {}
Submit(p,s,t,kind) ==
    LET enabled == IF kind = "outcome" THEN s.tasks[t].phase = "computed"
                   ELSE s.tasks[t].phase = "outcome-chosen"
        value == s.tasks[t].result
        nextPhase == IF kind = "outcome" THEN "outcome-wait" ELSE "decision-wait"
    IN IF enabled
       THEN {Transition("submit-" \o kind,
            [s EXCEPT !.tasks[t].phase = nextPhase],
            [w \in 1..3 |->
              EventFor(p,<<t,kind,w>>,p.actor,Witness(w),"Persist",
                       [task |-> t,kind |-> kind,bytes |-> value,witness |-> w])])}
       ELSE {}

Receive(p,s,e) ==
    IF e.kind = "Persist"
    THEN LET w == e.body.witness
             t == e.body.task
             kind == e.body.kind
         IN IF s.records[w][t][kind].phase = "none" /\
               RecordsAt(s,w) + s.filler[w] < p.ordinaryRecords + 2*Cardinality(p.tasks)
            THEN {Transition("queue-persist",
                    [s EXCEPT !.records[w][t][kind] =
                                [phase |-> "queued",bytes |-> e.body.bytes,acked |-> FALSE]],
                    <<>>)}
            ELSE IF s.records[w][t][kind].phase # "none" /\
                    s.records[w][t][kind].bytes = e.body.bytes
                 THEN {Transition("duplicate-persist",s,<<>>)}
                 ELSE {}
    ELSE IF e.kind = "Persisted"
    THEN LET t == e.body.task
             kind == e.body.kind
             w == e.body.witness
             active == s.tasks[t].phase =
                         IF kind = "outcome" THEN "outcome-wait" ELSE "decision-wait"
         IN IF e.body.bytes = s.tasks[t].result /\ active
            THEN {Transition("receive-persisted",
                    [s EXCEPT !.tasks[t].outcomeAcks =
                          IF kind = "outcome" THEN @ \cup {w} ELSE @,
                       !.tasks[t].decisionAcks =
                          IF kind = "decision" THEN @ \cup {w} ELSE @], <<>>)}
            ELSE {Transition("ignore-retired-receipt",s,<<>>)}
    ELSE IF e.kind = "SourceReady"
    THEN {Transition("receive-dependency",
            [s EXCEPT !.sourceBytes[e.body.task] = e.body.bytes,
                      !.availableSources = @ \cup {e.body.task}],<<>>)}
    ELSE {}
Persist(p,s,w,t,kind) ==
    IF s.records[w][t][kind].phase = "queued" /\
       (~p.slowThird \/ w # 3 \/ AnyDone(s))
    THEN {Transition("persist",
           [s EXCEPT !.records[w][t][kind].phase = "durable",
                     !.writes = @ \cup {[witness |-> w,task |-> t,kind |-> kind,
                                        bytes |-> s.records[w][t][kind].bytes]}], <<>>)}
    ELSE {}
Ack(p,s,w,t,kind) ==
    LET record == s.records[w][t][kind]
    IN IF ~record.acked /\
          (record.phase = "durable" \/
           (p.mutant = "early-ack" /\ record.phase = "queued"))
       THEN {Transition("ack",
               [s EXCEPT !.records[w][t][kind].acked = TRUE],
               <<EventFor(p,<<t,kind,w>>,Witness(w),p.actor,"Persisted",
                          [task |-> t,kind |-> kind,bytes |-> record.bytes,witness |-> w])>>)}
       ELSE {}
Choose(p,s,t,kind) ==
    LET acks == IF kind = "outcome" THEN s.tasks[t].outcomeAcks
                ELSE s.tasks[t].decisionAcks
        waiting == IF kind = "outcome" THEN "outcome-wait" ELSE "decision-wait"
        quorum == Cardinality(acks) >= IF p.mutant = "one-receipt" THEN 1 ELSE 2
    IN IF s.tasks[t].phase = waiting /\ quorum
       THEN {Transition("choose-" \o kind,
              [s EXCEPT !.tasks[t].phase =
                   IF kind = "outcome" THEN "outcome-chosen" ELSE "decided",
                        !.tasks[t].outcomeAcks = IF kind = "outcome" THEN {} ELSE @,
                        !.tasks[t].decisionAcks = IF kind = "decision" THEN {} ELSE @], <<>>)}
       ELSE {}
Finish(p,s,t) ==
    LET history == [r \in Resources |-> IF r \in {"record1","record2","record3"}
                                       THEN 2 ELSE 0]
    IN IF s.tasks[t].phase = "decided"
       THEN {Transition("finish",
               [s EXCEPT !.pool = step.next, !.tasks[t].phase = "done",
                         !.tasks[t].replied = TRUE,
                         !.reported = @ \cup {[task |-> t,bytes |-> s.tasks[t].result,
                                               reason |-> s.tasks[t].reason]}],
               <<EventFor(p,t,p.actor,"client","Done",
                          [task |-> t,bytes |-> s.tasks[t].result])>> \o
               (IF p.dependent /\ t="task1"
                THEN <<EventFor(p,<<"dependency",t>>,p.actor,"runtime","SourceReady",
                       [task |-> "task2",predecessor |-> t,
                        bytes |-> IF s.tasks[t].reason="success"
                                  THEN s.tasks[t].result ELSE s.source])>>
                ELSE <<>>))
             : step \in Resize(p.capacities,s.pool,BundleId(t),history)}
       ELSE {}

\* A live journal/history obligation holds ordinary records until its dependent
\* transaction finishes. The defect permits these writes into completion space.
FillOrdinary(p,s) ==
    IF p.pressure /\ ~s.ordinaryFilled /\ AllAdmitted(s) /\
       \A w \in 1..3 : RecordsAt(s,w) = 0
    THEN {Transition("fill-ordinary",
            [s EXCEPT !.ordinaryFilled = TRUE,
                      !.filler = [w \in 1..3 |->
                         p.ordinaryRecords +
                           IF p.mutant = "spend-control" THEN 2*Cardinality(p.tasks) ELSE 0]], <<>>)}
    ELSE {}
RetireOrdinary(p,s) ==
    IF AnyDone(s) /\ \E w \in 1..3 : s.filler[w] > 0
    THEN {Transition("retire-ordinary", [s EXCEPT !.filler = [w \in 1..3 |-> 0]], <<>>)}
    ELSE {}

\* Shared physical result: optional subscriber cancellation never releases a
\* required subscriber's use or the queued/active backend borrow.
StartShared(p,s) ==
    IF p.shared /\ s.shared = "idle"
    THEN {Transition("shared-submit",
            [s EXCEPT !.shared = "queued", !.sharedCharged = TRUE,
                      !.pool = step.next], <<>>)
          : step \in Request(p.capacities,s.pool,
              Lease(<<"shared","result">>,"subscribers","physical-borrow",
                    [ZeroDemand EXCEPT !["working"] = 1, !["io"] = 1]))}
    ELSE {}
CancelOptional(p,s) ==
    IF p.shared /\ ~s.cancelledOptional /\ s.shared \in {"queued","active","complete"}
    THEN {Transition("cancel-optional",
            [s EXCEPT !.cancelledOptional = TRUE,
                      !.subscribers = @ \ {"optional"},
                      !.sharedCharged = IF p.mutant = "cancel-debt" THEN FALSE ELSE @], <<>>)}
    ELSE {}
SharedStep(p,s) ==
    CASE s.shared = "queued" ->
           {Transition("shared-start",[s EXCEPT !.shared = "active"],<<>>)}
      [] s.shared = "active" ->
           {Transition("shared-complete",[s EXCEPT !.shared = "complete"],<<>>)}
      [] s.shared = "complete" ->
           {Transition("shared-retire",
              [s EXCEPT !.shared = "retired",!.sharedCharged = FALSE,
                        !.subscribers = {}, !.pool = step.next],<<>>)
             : step \in Release(s.pool,<<"shared","result">>)}
      [] OTHER -> {}
Probe(p,s) ==
    IF p.alarms /\ (~s.alarmSeen \/ (p.mutant = "duplicate-probes" /\ 2 \notin s.probes))
    THEN LET id == IF s.alarmSeen THEN 2 ELSE 1
         IN {Transition("probe",
               [s EXCEPT !.alarmSeen = TRUE, !.probes = @ \cup {id},
                         !.pool = step.next],<<>>)
             : step \in Request(p.capacities,s.pool,
                 Lease(<<"probe",ToString(id)>>,"health","probe",
                       [ZeroDemand EXCEPT !["probe"] = IF id=1 THEN 1 ELSE 0]))}
    ELSE {}
RetireProbe(p,s,id) ==
    IF id \in s.probes \ s.probeRetired
    THEN {Transition("probe-retire",
            [s EXCEPT !.probeRetired = @ \cup {id}, !.pool = step.next],<<>>)
          : step \in Release(s.pool,<<"probe",ToString(id)>>)}
    ELSE {}
Restart(p,s) ==
    IF p.crash /\ ~s.restarted /\
       \E t \in p.tasks : s.tasks[t].phase \in {"outcome-wait","decision-wait"}
    THEN {Transition("restart-driver",
            [s EXCEPT !.restarted = TRUE, !.incarnation = 2],<<>>)}
    ELSE {}

Actions(p,s) ==
    UNION {Admit(p,s,t) \cup Start(p,s,t) \cup Touch(p,s,t) \cup FaultStart(p,s,t) \cup
           Execute(p,s,t) \cup Cancel(p,s,t) \cup Finish(p,s,t) \cup
           UNION {Submit(p,s,t,k) \cup Choose(p,s,t,k) : k \in Phases}
           : t \in p.tasks}
    \cup UNION {Persist(p,s,w,t,k) \cup Ack(p,s,w,t,k)
                : <<w,t,k>> \in (1..3) \X p.tasks \X Phases}
    \cup FaultRead(p,s) \cup FaultWake(p,s)
    \cup FillOrdinary(p,s) \cup RetireOrdinary(p,s)
    \cup StartShared(p,s) \cup CancelOptional(p,s) \cup SharedStep(p,s)
    \cup Probe(p,s) \cup UNION {RetireProbe(p,s,id) : id \in {1,2}}
    \cup Restart(p,s)
TasksDone(p,s) == \A t \in p.tasks : s.tasks[t].phase = "done"
Terminal(p,s) ==
    /\ TasksDone(p,s)
    /\ s.fault.phase = "idle"
    /\ ~p.shared \/ s.shared = "retired"
    /\ s.probes = s.probeRetired
    /\ \A w \in 1..3 : s.filler[w] = 0
=============================================================================
