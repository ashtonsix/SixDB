------------------------- MODULE CapacityProgress -------------------------
EXTENDS Naturals, Integers, Sequences, FiniteSets, TLC, Contracts
CONSTANTS TaskCount, Workers, Scenario, Mutant
VARIABLES service, network, observed, unrelated
vars == <<service,network,observed,unrelated>>
C == INSTANCE CapacityKernel

\* This family exercises a prepared durable quorum resource path. It does not
\* supply competing-ballot agreement or transaction serial semantics. C7 replaces
\* these fixture requests with Journal/Transaction/Recovery transitions using the
\* same named lease operations and physical-lifetime accounting.
Tasks == IF TaskCount = 1 THEN {"task1"} ELSE {"task1","task2"}
Shared == Scenario \in {"shared","shared-dependent","combined"}
Capacities ==
    [r \in C!Resources |->
       CASE r = "working" -> TaskCount + IF Shared THEN 1 ELSE 0
         [] r \in {"record1","record2","record3"} -> 2*TaskCount
         [] r \in {"metadata","send"} -> TaskCount
         [] OTHER -> 1]
Params ==
    [actor |-> "owner", tasks |-> Tasks, capacities |-> Capacities,
     workers |-> Workers, source |-> 17, ordinaryRecords |-> 1,
     mutant |-> Mutant, cancel |-> Scenario \in {"cancel","combined"},
     pressure |-> Scenario \in {"pressure","shared-dependent","combined"},
     shared |-> Shared, alarms |-> Scenario \in {"alarms","combined"},
     slowThird |-> Scenario \in {"slow-third","shared-dependent","combined"},
     dependent |-> TaskCount > 1 /\ Scenario \in {"dependent","shared-dependent","combined"},
     crash |-> Scenario \in {"restart","combined"}]
Transport(events) == {e \in events : e.kind \in {"Persist","Persisted","SourceReady"}}
Reports(events) == events \ Transport(events)
Init ==
    /\ service = C!Init(Params)
    /\ network = {}
    /\ observed = {}
    /\ unrelated = FALSE
\* This joined workload starts a shared send, parks both consumers, then lets
\* that send finish before servicing the first page fault. Its real I/O lease
\* prevents the resolver from starting until physical retirement. We explore
\* cancellation/start order within that seam, not every independent timing of
\* a shared send across both complete quorum histories.
SharedSchedule(step) ==
    IF Scenario # "shared-dependent" THEN TRUE
    ELSE IF service.shared = "idle" THEN step.tag = "shared-submit"
    ELSE IF service.shared # "retired"
         THEN /\ step.tag \in {"admit","start","first-touch","fill-ordinary",
                               "shared-start","cancel-optional",
                               "shared-complete","shared-retire"}
              /\ step.tag \in {"shared-complete","shared-retire"} =>
                   (service.ordinaryFilled /\ service.cancelledOptional /\
                    \A t \in Tasks : service.tasks[t].phase = "faulted")
         ELSE TRUE
Local ==
    \E step \in C!Actions(Params,service) :
      /\ SharedSchedule(step)
      /\ service' = step.next
      /\ network' = network \cup Transport(Elements(step.emissions))
      /\ observed' = observed \cup Reports(Elements(step.emissions))
      /\ UNCHANGED unrelated
\* The dependent workload's second input is delivered after its producer's
\* service tail drains. Both consumers are already admitted/parked before then.
\* This is an authored application-delivery boundary, not a quotient of the
\* unrestricted independent two-pipeline pilot (retained separately).
ProducerTailDrained ==
    /\ \A w \in 1..3 :
         \A k \in C!Phases : service.records[w]["task1"][k].phase="durable" /\
                             service.records[w]["task1"][k].acked
    /\ ~(\E e \in network : e.kind \in {"Persist","Persisted"} /\ e.body.task="task1")
Deliver ==
    \E e \in network :
      /\ e.kind # "SourceReady" \/ ProducerTailDrained
      /\ \E step \in C!Receive(Params,service,e) :
        /\ service' = step.next
        /\ network' = (network \ {e}) \cup Transport(Elements(step.emissions))
        /\ observed' = observed \cup Reports(Elements(step.emissions))
        /\ UNCHANGED unrelated
UsefulStep == Local \/ Deliver
Done ==
    /\ C!Terminal(Params,service)
    /\ network = {}
    /\ \A w \in 1..3 :
         \A t \in Tasks :
           \A k \in C!Phases : service.records[w][t][k].phase = "durable"
Unrelated ==
    /\ ~Done
    /\ unrelated' = ~unrelated
    /\ UNCHANGED <<service,network,observed>>
Next == UsefulStep \/ Unrelated \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(UsefulStep) /\ WF_vars(Unrelated)

\* The concrete records and backend objects are counted independently of the
\* pool's reservation arithmetic. Reserved-but-unused credits remain promises.
PoolBounds ==
    \A r \in C!Resources : C!Usage(service.pool,r) <= Capacities[r]
PhysicalMemory ==
    LET scratch == Cardinality({t \in Tasks :
                      service.tasks[t].phase \notin {"offered","done"}})
        shared == IF service.shared \in {"queued","active","complete"} THEN 1 ELSE 0
        fault == IF service.fault.phase \in {"queued","complete"} THEN 1 ELSE 0
    IN /\ scratch + shared <= Capacities["working"]
       /\ fault <= Capacities["fault-memory"]
PhysicalLeases ==
    /\ \A t \in Tasks :
         service.tasks[t].phase \notin {"offered","done"} =>
           C!BundleId(t) \in DOMAIN service.pool.leases
    /\ (service.fault.phase # "idle") =>
         C!FaultId(service.fault.task) \in DOMAIN service.pool.leases
    /\ service.sharedCharged =
         (service.shared \in {"queued","active","complete"})
    /\ (service.shared \in {"queued","active","complete"}) =>
         <<"shared","result">> \in DOMAIN service.pool.leases
    /\ Cardinality(service.probes \ service.probeRetired) <= Capacities["probe"]
ReservedCompletion ==
    \A w \in 1..3 :
      service.filler[w] <= Params.ordinaryRecords /\
      C!RecordsAt(service,w) <= 2*TaskCount
ActualQuorum(t,kind) ==
    Cardinality({w \in 1..3 :
        service.records[w][t][kind].phase = "durable" /\
        service.records[w][t][kind].bytes = service.tasks[t].result}) >= 2
OutcomeDurable ==
    \A t \in Tasks :
      service.tasks[t].phase \in {"outcome-chosen","decision-wait","decided","done"} =>
        ActualQuorum(t,"outcome")
DecisionDurable ==
    \A t \in Tasks : service.tasks[t].replied => ActualQuorum(t,"decision")
ExpectedInput(t) ==
    IF Params.dependent /\ t="task2"
    THEN IF service.tasks["task1"].reason="success" THEN 18 ELSE 17
    ELSE 17
ObservedBytes ==
    \A t \in Tasks :
      service.tasks[t].phase \in {"ready","computed","outcome-wait","outcome-chosen",
                                 "decision-wait","decided","done"} =>
        service.tasks[t].observed = ExpectedInput(t)
Results ==
    \A r \in service.reported :
      IF r.reason = "cancel"
      THEN r.bytes = -1 /\ service.tasks[r.task].cancelled
      ELSE r.reason = "success" /\ r.bytes = ExpectedInput(r.task)+1
SharedObligation ==
    service.shared \in {"queued","active","complete"} =>
      "required" \in service.subscribers
BoundedInvestigation == Cardinality(service.probes) <= 1
Complete == <>Done
TasksComplete == <>C!TasksDone(Params,service)
NoPressureWitness == ~service.ordinaryFilled
NoFaultAtCapacityWitness ==
    ~(service.fault.phase = "queued" /\
      Cardinality({t \in Tasks : service.tasks[t].worker}) = Workers)
NoQuorumBeforeThirdWitness ==
    ~(\E t \in Tasks : service.tasks[t].replied /\
        service.records[3][t]["decision"].phase # "durable")
NoSharedCancelWitness ==
    ~(service.cancelledOptional /\ service.sharedCharged /\
      "required" \in service.subscribers)
NoFailureDecisionWitness ==
    ~(\E r \in service.reported : r.reason = "cancel")
NoDriverDebtWitness ==
    ~(service.restarted /\ \E t \in Tasks :
       service.tasks[t].phase \in {"outcome-wait","decision-wait"})
NoTwoCompletionWitness ==
    ~(Cardinality(service.reported) = 2)
NoDependentPressureWitness ==
    ~(Params.dependent /\ C!OrdinaryWorkers(service)=Workers /\
      service.fault.phase="queued" /\ service.fault.task="task1" /\
      "task2" \notin service.availableSources)
NoBorrowBlocksFaultWitness ==
    ~(service.shared="complete" /\ service.cancelledOptional /\
      service.fault.phase="idle" /\ service.ordinaryFilled /\
      \A t \in Tasks : service.tasks[t].phase="faulted")
=============================================================================
