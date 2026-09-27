--------------------------- MODULE ViewsRuntime ---------------------------
EXTENDS Naturals, Integers, Sequences, FiniteSets, TLC, Contracts
CONSTANTS Scenario, PageCount, Mutant, Prepared
VARIABLES runtime, inputs, outputs
vars == <<runtime, inputs, outputs>>
V == INSTANCE ViewsKernel

\* This consumer family starts with a named retained root and delivers actual
\* grant/material records. It does not prove their creation: the RootKernel
\* composition substitutes its own emitted events for these fixture inputs.
Indices == 1..(2 * PageCount)
Base == [i \in Indices |-> i * 10]
ViewIds == IF Scenario \in {"sharing", "undo"} THEN {}
           ELSE IF Scenario = "combined" THEN {"reader", "checker"}
           ELSE {"reader"}
WriterIds == IF Scenario \in {"sharing", "undo", "combined"} THEN {"w1", "w2"}
             ELSE IF Scenario \in {"reuse","reuse-abort"} THEN {"w1"} ELSE {}
OpIds == IF ViewIds = {} THEN {} ELSE {"send"}
Rights(v) == IF Scenario \in {"rights","representation"} \/ v = "checker"
             THEN [read |-> {1}, write |-> {}]
             ELSE [read |-> Indices, write |-> {}]
Params ==
    [actor |-> "runtime", owner |-> "owner", base |-> Base,
     views |-> ViewIds, writers |-> WriterIds, operations |-> OpIds,
     pages |-> 1..PageCount,
     pageItems |-> [pg \in 1..PageCount |-> {2*pg-1, 2*pg}],
     readScope |-> [v \in ViewIds |-> Indices],
     opView |-> [o \in OpIds |-> "reader"],
     opScope |-> [o \in OpIds |-> Indices],
     writeScope |-> [w \in WriterIds |-> IF w = "w1" THEN {1} ELSE {2}],
     writeValues |-> [w \in WriterIds |-> [i \in Indices |-> i*10 + 1]],
     noCOW |-> Scenario \in {"reuse", "reuse-abort","combined"}, reuseWriter |-> "w1",
     restoreRecipe |-> "initial", restoreInterpretation |-> "bytes-v1",
     cancel |-> Scenario \in {"reuse", "lifetime", "combined"},
     crash |-> Scenario \in {"lifetime", "combined"},
     hostReset |-> Scenario = "host-reset",
     rebind |-> Scenario = "lifetime", prepared |-> Prepared, mutant |-> Mutant,
     cpuWrite |-> Scenario = "rights"]
Grant(v) ==
    Event(<<"grant", v>>, "owner", "runtime", "ViewGrant",
          [view |-> v, context |-> <<"context",v>>, c |-> 0,
           root |-> <<"root",v>>, generation |-> 1, recipe |-> "initial",
           interpretation |-> "bytes-v1", rights |-> Rights(v)])
Material ==
    Event(<<"material","base">>, "holder", "runtime", "Material",
          [recipe |-> "initial", interpretation |-> "bytes-v1",
           copy |-> "durable-base", storageIncarnation |-> 1, bytes |-> Base])
AlternateMaterial ==
    Event(<<"material","alternate">>,"holder","runtime","Material",
          [recipe |-> "initial",interpretation |-> "encoded-v2",
           copy |-> "alternate-base",storageIncarnation |-> 1,
           bytes |-> [i \in Indices |-> Base[i]+7]])
InitialInputs ==
    {Grant(v) : v \in ViewIds} \cup
    (IF ViewIds = {} THEN {} ELSE {Material}) \cup
    (IF Scenario = "representation" THEN {AlternateMaterial} ELSE {})
Decision(w) ==
    Event(<<"decision",w>>, "owner", "runtime", "Decision",
          [writer |-> w, outcome |-> IF (Scenario = "undo" /\ w = "w2") \/
                                        Scenario = "reuse-abort"
                                    THEN "abort" ELSE "commit",
           record |-> <<"chosen-decision",w>>])

Init ==
    /\ runtime = V!Init(Params)
    /\ inputs = InitialInputs
    /\ outputs = {}
Local ==
    \E step \in V!Actions(Params,runtime) :
        /\ runtime' = step.next
        /\ outputs' = outputs \cup Elements(step.emissions)
        /\ UNCHANGED inputs
Deliver ==
    \E e \in inputs :
      \E step \in V!Receive(Params,runtime,e) :
        /\ runtime' = step.next
        /\ inputs' = inputs \ {e}
        /\ outputs' = outputs \cup Elements(step.emissions)
Decide ==
    \E w \in WriterIds :
        /\ runtime.writers[w].phase = "sealed"
        /\ runtime.writers[w].decision = ""
        /\ Decision(w) \notin inputs
        /\ inputs' = inputs \cup {Decision(w)}
        /\ UNCHANGED <<runtime,outputs>>
Done == V!Terminal(Params,runtime) /\ inputs = {}
Next == Local \/ Deliver \/ Decide \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Local \/ Deliver \/ Decide)

\* Independent byte oracle: fixture old-cut input is Base, while each distinct
\* committed scope contributes its own update. Neither drives an action guard.
ExactObserved ==
    \A obs \in runtime.observations :
      \A i \in obs.extent :
        obs.bytes[i] = IF i \in obs.rights.read \cup obs.rights.write
                       THEN Base[i] ELSE 0
PublishedBytes ==
    \A i \in Indices :
      runtime.installed[i] =
        IF \E w \in WriterIds :
             runtime.writers[w].installed /\ i \in Params.writeScope[w]
        THEN Base[i] + 1 ELSE Base[i]
BorrowedBytes ==
    \A o \in OpIds :
      runtime.operations[o].phase \in {"complete","retired"} /\
      ~runtime.operations[o].cancelledByHost =>
        \A i \in runtime.operations[o].extent :
           runtime.operations[o].result[i] = runtime.operations[o].expected[i]
AuthorizedAsync ==
    \A o \in OpIds :
      runtime.operations[o].phase \notin {"none","refused"} =>
        runtime.operations[o].extent \subseteq
          runtime.views[runtime.operations[o].view].grant.rights.read
PhysicalCharges ==
    \A o \in OpIds :
      runtime.operations[o].charged =
        (runtime.operations[o].phase \in {"queued","active","complete","cancelled"})
CallbacksFenced ==
    \A c \in runtime.callbacks : c.submitter = c.receiver
BindingsHeld ==
    \A o \in OpIds :
      runtime.operations[o].phase \in {"queued","active","complete"} =>
        runtime.operations[o].slotGeneration =
          runtime.copyGeneration[runtime.operations[o].slot]
MaterializedIdentity ==
    \A v \in ViewIds :
      runtime.views[v].phase = "mapped" =>
        V!MaterialFor(runtime,runtime.views[v].grant) # {}
Complete == <>Done
NoReuseWitness ==
    ~(\E w \in WriterIds : runtime.writers[w].reused /\ runtime.writers[w].phase = "done")
NoLateOldWitness ==
    ~(\E v \in ViewIds :
        runtime.views[v].slot = v /\ runtime.views[v].read /\
        \E w \in WriterIds : runtime.writers[w].reused /\ runtime.writers[w].phase = "done")
NoCancelDebtWitness ==
    ~(\E o \in OpIds : runtime.operations[o].cancelled /\ runtime.operations[o].charged)
NoRestartRetirementWitness ==
    ~(runtime.restarted /\ \E o \in OpIds :
        runtime.operations[o].phase = "retired" /\
        runtime.operations[o].submittingIncarnation = 1)
NoTwoWriterWitness ==
    ~(\A w \in WriterIds : runtime.writers[w].installed)
NoDeniedWitness == runtime.denials = {}
NoTwoPageWitness ==
    ~(\E v \in ViewIds : Cardinality(runtime.views[v].pages) = 2 /\ runtime.views[v].read)
RegistryDrainSound ==
    runtime.registry.drained =>
       \A o \in DOMAIN runtime.registry.operations :
          runtime.registry.operations[o].phase = "retired"
RegistryMirrorsBackend ==
    \A o \in DOMAIN runtime.registry.operations :
      runtime.operations[o].phase = runtime.registry.operations[o].phase
NoHostCancellationWitness ==
    ~(runtime.hostReset /\ \E o \in OpIds :
         runtime.operations[o].cancelledByHost /\ runtime.operations[o].phase = "retired")
NoDrainWitness == ~runtime.registry.drained
NoRestoreWitness ==
    ~(\E w \in WriterIds : runtime.writers[w].reused /\
        runtime.writers[w].phase = "done" /\ runtime.writers[w].decision = "abort" /\
        runtime.copies["shared"] = Base)
ReadOnlyProjection ==
    \A v \in ViewIds :
      LET view == runtime.views[v]
      IN view.phase = "mapped" /\ ~view.closed =>
         \A i \in view.grant.rights.read \ view.grant.rights.write :
           V!PagesFor(Params,{i}) \subseteq view.pages =>
             runtime.copies[view.slot][i] = Base[i]
NoCPUWriteDenialWitness == runtime.writeDenials = {}
=============================================================================
