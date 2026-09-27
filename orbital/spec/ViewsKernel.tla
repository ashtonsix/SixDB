--------------------------- MODULE ViewsKernel ---------------------------
EXTENDS Naturals, Integers, Sequences, FiniteSets, TLC, Contracts

\* Backend registry survives execution-context death. Root's physical completion
\* transition applies BackendCompleted.payload to the named target in the same
\* composed step; callback delivery/retirement is later. No metadata publication
\* follows merely from completion of a cache write.
RegistryInit ==
    [generation |-> 1, open |-> TRUE, drained |-> FALSE,
     operations |-> [id \in {} |-> id]]
RegistryRegister(s, request) ==
    IF s.open /\ request.generation = s.generation /\
       request.id \notin DOMAIN s.operations
    THEN {Transition("registry-register",
            [s EXCEPT !.operations =
               @ @@ (request.id :> [request |-> request, phase |-> "queued"])], <<>>)}
    ELSE {}
RegistryClose(s) ==
    IF s.open
    THEN {Transition("registry-close", [s EXCEPT !.open = FALSE], <<>>)}
    ELSE {}
RegistryReopen(s) ==
    IF s.drained
    THEN {Transition("registry-reopen",
           [s EXCEPT !.generation = @ + 1, !.open = TRUE, !.drained = FALSE],<<>>)}
    ELSE {}
RegistryReset(s) ==
    {Transition("registry-host-reset",
       [s EXCEPT !.open = FALSE,
          !.operations = [id \in DOMAIN @ |->
             IF @[id].phase \in {"queued","active"}
             THEN [@[id] EXCEPT !.phase = "cancelled"] ELSE @[id]]], <<>>)}
RegistryDebt(s) ==
    {id \in DOMAIN s.operations :
        s.operations[id].phase \in {"queued","active","complete","cancelled"}}
RegistryOperation(p, s, id) ==
    LET op == s.operations[id]
        emit(kind) == Event(<<p.actor,id,kind>>,p.actor,p.owner,kind,op.request)
    IN CASE op.phase = "queued" ->
             {Transition("backend-start",
                 [s EXCEPT !.operations[id].phase = "active"], <<>>)}
         [] op.phase = "active" ->
             {Transition("backend-complete",
                 [s EXCEPT !.operations[id].phase = "complete"],
                 <<emit("BackendCompleted")>>)}
         [] op.phase \in {"complete","cancelled"} ->
             {Transition("backend-retire",
                 [s EXCEPT !.operations[id].phase = "retired"],
                 <<emit("BackendRetired")>>)}
         [] OTHER -> {}
RegistryDrain(p,s) ==
    IF ~s.open /\ ~s.drained /\
       (RegistryDebt(s) = {} \/
        IF "mutant" \in DOMAIN p THEN p.mutant = "early-drain" ELSE FALSE)
    THEN {Transition("registry-drained", [s EXCEPT !.drained = TRUE],
           <<Event(<<p.actor,s.generation,"drained">>,p.actor,p.owner,
                   "RegistryDrained",[generation |-> s.generation])>>)}
    ELSE {}
RegistryActions(p,s) ==
    UNION {RegistryOperation(p,s,id) : id \in DOMAIN s.operations}
    \cup RegistryDrain(p,s)

\* A local consumer/runtime. Root registration, application decisions and
\* physical storage are providers; incoming bytes/evidence are never inferred
\* from an observer's inventory. All actions return immutable local transitions.
Items(p) == 1..Len(p.base)
Zero(p) == [i \in Items(p) |-> 0]
Mask(bytes, rights) ==
    [i \in DOMAIN bytes |-> IF i \in rights.read \cup rights.write
                            THEN bytes[i] ELSE 0]
Patch(bytes, scope, values) ==
    [i \in DOMAIN bytes |-> IF i \in scope THEN values[i] ELSE bytes[i]]
Slice(bytes, extent) == [i \in extent |-> bytes[i]]
EmptyGrant ==
    [view |-> "", context |-> "", c |-> 0, root |-> "", generation |-> 0,
     recipe |-> "", interpretation |-> "", rights |-> [read |-> {}, write |-> {}]]
EmptyView ==
    [phase |-> "absent", grant |-> EmptyGrant, slot |-> "", pages |-> {},
     read |-> FALSE, closed |-> FALSE]
EmptyWriter(p) ==
    [phase |-> "idle", slot |-> "", before |-> Zero(p), decision |-> "",
     installed |-> FALSE, reused |-> FALSE]
EmptyOp(p) ==
    [phase |-> "none", view |-> "", slot |-> "", slotGeneration |-> 0,
     submittingIncarnation |-> 0, extent |-> {}, expected |-> Zero(p),
     result |-> Zero(p), cancelled |-> FALSE, charged |-> FALSE,
     cancelledByHost |-> FALSE]

Init(p) ==
    [views |-> [v \in p.views |-> EmptyView],
     writers |-> [w \in p.writers |-> EmptyWriter(p)],
     operations |-> [o \in p.operations |-> EmptyOp(p)],
     copies |-> [slot \in {"shared"} \cup p.views \cup p.writers |-> p.base],
     copyGeneration |-> [slot \in {"shared"} \cup p.views \cup p.writers |-> 1],
     materials |-> {}, gate |-> TRUE, installed |-> p.base, incarnation |-> 1,
     restarted |-> FALSE, rebound |-> FALSE, observations |-> {},
     callbacks |-> {}, denials |-> {}, received |-> {}, closedViews |-> {},
     registry |-> RegistryInit, hostReset |-> FALSE,
     writeAttempts |-> {}, writeDenials |-> {}]

Emit(p, key, kind, body) ==
    Event(<<p.actor, key, kind>>, p.actor, p.owner, kind, body)
ViewUsers(s, slot) ==
    {v \in DOMAIN s.views : s.views[v].slot = slot /\ ~s.views[v].closed
                           /\ s.views[v].phase = "mapped"}
BorrowUsers(s, slot) ==
    {o \in DOMAIN s.operations :
        s.operations[o].slot = slot /\ s.operations[o].charged}
WriterUsers(s, slot) ==
    {w \in DOMAIN s.writers : s.writers[w].slot = slot /\
                              s.writers[w].phase \in {"open", "sealed"}}
Users(s, slot) == ViewUsers(s, slot) \cup BorrowUsers(s, slot) \cup WriterUsers(s, slot)
PhysicalUsers(s, slot) ==
    {o \in DOMAIN s.operations :
        s.operations[o].slot = slot /\
        s.operations[o].phase \in {"queued", "active", "complete"}}
MaterialFor(s, grant) ==
    {m \in s.materials : m.recipe = grant.recipe /\
                         m.interpretation = grant.interpretation}
EligibleMaterial(p,s,grant) ==
    IF p.mutant = "wrong-interpretation"
    THEN {m \in s.materials : m.recipe = grant.recipe}
    ELSE MaterialFor(s,grant)
PagesFor(p, extent) == {pg \in p.pages : p.pageItems[pg] \cap extent # {}}
StartedOps(p, s, v) ==
    \A o \in p.operations : p.opView[o] = v => s.operations[o].phase # "none"

Receive(p, s, e) ==
    IF e.id \in s.received THEN {}
    ELSE IF e.kind = "ViewGrant" /\ e.body.view \in p.views /\
            s.views[e.body.view].phase = "absent" /\
            e.body.rights.read \cup e.body.rights.write \subseteq Items(p)
    THEN {Transition("receive-grant",
           [s EXCEPT !.received = @ \cup {e.id},
                     !.views[e.body.view] =
                       [EmptyView EXCEPT !.phase = "granted", !.grant = e.body]], <<>>)}
    ELSE IF e.kind = "Material" /\ DOMAIN e.body.bytes = Items(p)
    THEN {Transition("receive-material",
           [s EXCEPT !.received = @ \cup {e.id}, !.materials = @ \cup {e.body}], <<>>)}
    ELSE IF e.kind = "Decision" /\ e.body.writer \in p.writers
    THEN {Transition("receive-decision",
           [s EXCEPT !.received = @ \cup {e.id},
                     !.writers[e.body.writer].decision = e.body.outcome], <<>>)}
    ELSE {}

MapView(p, s, v) ==
    LET view == s.views[v]
        allRights == view.grant.rights.read \cup view.grant.rights.write = Items(p)
        shared == allRights /\ view.grant.recipe = p.restoreRecipe /\
                  view.grant.interpretation = p.restoreInterpretation /\
                  (s.gate \/ p.mutant = "late-user")
        slot == IF shared THEN "shared" ELSE v
    IN IF view.phase = "granted" /\ EligibleMaterial(p,s, view.grant) # {}
       THEN {Transition("map",
              [s EXCEPT !.views[v].phase = "mapped", !.views[v].slot = slot,
                        !.copies[v] = Zero(p)], <<>>)}
       ELSE {}

ResolvePage(p, s, v, pg) ==
    LET view == s.views[v]
        candidates == EligibleMaterial(p,s, view.grant)
    IN IF view.phase = "mapped" /\ ~view.closed /\ pg \notin view.pages /\
          candidates # {}
       THEN {LET allowed == IF p.mutant = "adjacent-bytes"
                            THEN [read |-> Items(p), write |-> {}]
                            ELSE view.grant.rights
                 bytes == Mask(m.bytes, allowed)
                 pages == IF p.prepared THEN p.pages ELSE {pg}
                 extent == UNION {p.pageItems[q] : q \in pages}
                 nextBytes == IF view.slot = "shared" THEN s.copies["shared"]
                              ELSE Patch(s.copies[view.slot], extent, bytes)
             IN Transition("resolve-page",
                 [s EXCEPT !.views[v].pages = @ \cup pages,
                           !.copies[view.slot] = nextBytes], <<>>)
             : m \in candidates}
       ELSE {}

ReadView(p, s, v) ==
    LET view == s.views[v]
        extent == p.readScope[v]
        obs == [view |-> v, context |-> view.grant.context, c |-> view.grant.c,
                extent |-> extent, bytes |-> Slice(s.copies[view.slot], extent),
                rights |-> view.grant.rights, recipe |-> view.grant.recipe]
    IN IF view.phase = "mapped" /\ ~view.closed /\ ~view.read /\
          PagesFor(p, extent) \subseteq view.pages
       THEN {Transition("read",
              [s EXCEPT !.views[v].read = TRUE, !.observations = @ \cup {obs}],
              <<Emit(p, v, "Observe", obs)>>)}
       ELSE {}

CPUWriteAttempt(p,s,v) ==
    LET view == s.views[v]
        requested == {1}
        enabled == IF "cpuWrite" \in DOMAIN p THEN p.cpuWrite ELSE FALSE
        allowed == requested \subseteq view.grant.rights.write \/
                   p.mutant = "cpu-readonly"
    IN IF enabled /\ view.phase = "mapped" /\ ~view.closed /\
          v \notin s.writeAttempts /\ PagesFor(p,requested) \subseteq view.pages
       THEN {Transition("cpu-write-attempt",
               [s EXCEPT !.writeAttempts = @ \cup {v},
                         !.writeDenials = IF allowed THEN @ ELSE @ \cup {v},
                         !.copies[view.slot] =
                            IF allowed THEN Patch(@,requested,[i \in Items(p) |-> 99])
                            ELSE @],<<>>)}
       ELSE {}

CloseView(p, s, v) ==
    IF s.views[v].phase = "mapped" /\ ~s.views[v].closed /\
       s.views[v].read /\ StartedOps(p, s, v)
    THEN {Transition("close-view",
           [s EXCEPT !.views[v].closed = TRUE, !.closedViews = @ \cup {v}],
           <<Emit(p, v, "ViewClosed",
                  [view |-> v, root |-> s.views[v].grant.root,
                   generation |-> s.views[v].grant.generation])>>)}
    ELSE {}

BeginBorrow(p, s, o) ==
    LET v == p.opView[o]
        view == s.views[v]
        extent == p.opScope[o]
        permitted == extent \subseteq view.grant.rights.read
    IN IF s.operations[o].phase = "none" /\ view.phase = "mapped" /\
          ~view.closed /\ PagesFor(p, extent) \subseteq view.pages
       THEN IF permitted \/ p.mutant = "async-extents"
            THEN {Transition("borrow",
                   [s EXCEPT !.operations[o] =
                       [EmptyOp(p) EXCEPT
                         !.phase = "queued", !.view = v, !.slot = view.slot,
                         !.slotGeneration = s.copyGeneration[view.slot],
                         !.submittingIncarnation = s.incarnation,
                         !.extent = extent, !.expected = s.copies[view.slot],
                         !.charged = TRUE],
                             !.registry = step.next],
                   <<Emit(p, o, "BorrowStarted",
                          [operation |-> o, copy |-> view.slot,
                           generation |-> s.copyGeneration[view.slot]])>>)
                   : step \in RegistryRegister(s.registry,
                       [id |-> o, generation |-> s.registry.generation,
                        target |-> view.slot, kind |-> "read",
                        payload |-> [extent |-> extent,
                                     copyGeneration |-> s.copyGeneration[view.slot]]])}
            ELSE {Transition("deny-async",
                   [s EXCEPT !.operations[o].phase = "refused",
                             !.denials = @ \cup {o}], <<>>)}
       ELSE {}

Backend(p, s, o) ==
    LET op == s.operations[o]
    IN IF o \in DOMAIN s.registry.operations
       THEN {LET complete == step.tag = "backend-complete"
                 retire == step.tag = "backend-retire"
                 deliver == retire /\ ~op.cancelledByHost /\
                            (op.submittingIncarnation = s.incarnation \/
                             p.mutant = "stale-callback")
                 effect == [operation |-> o, receiver |-> s.incarnation,
                            submitter |-> op.submittingIncarnation,
                            bytes |-> Slice(op.result, op.extent)]
             IN Transition(step.tag,
                   [s EXCEPT !.registry = step.next,
                             !.operations[o].phase = step.next.operations[o].phase,
                             !.operations[o].result =
                                 IF complete THEN s.copies[op.slot] ELSE @,
                             !.operations[o].charged = IF retire THEN FALSE ELSE @,
                             !.callbacks = IF deliver THEN @ \cup {effect} ELSE @],
                   IF retire THEN <<Emit(p, o, "BorrowRetired",
                          [operation |-> o, copy |-> op.slot,
                           generation |-> op.slotGeneration])>>
                   ELSE step.emissions)
              : step \in RegistryOperation(p,s.registry,o)}
       ELSE {}

Cancel(p, s, o) ==
    IF p.cancel /\ s.operations[o].phase \in {"queued", "active", "complete"} /\
       ~s.operations[o].cancelled
    THEN {Transition("cancel",
           [s EXCEPT !.operations[o].cancelled = TRUE,
                     !.operations[o].charged =
                         IF p.mutant = "cancel-retires" THEN FALSE ELSE @], <<>>)}
    ELSE {}

CloseGate(p, s) ==
    IF p.noCOW /\ s.gate
    THEN {Transition("close-access-gate", [s EXCEPT !.gate = FALSE], <<>>)}
    ELSE {}

BeginWriter(p, s, w) ==
    LET useShared == p.noCOW /\ w = p.reuseWriter
        canReuse == ~s.gate /\ Users(s, "shared") = {}
    IN IF s.writers[w].phase = "idle" /\ (~useShared \/ canReuse)
       THEN {Transition("begin-writer",
              [s EXCEPT !.writers[w] =
                  [EmptyWriter(p) EXCEPT
                     !.phase = "open", !.slot = IF useShared THEN "shared" ELSE w,
                     !.before = IF useShared THEN Zero(p) ELSE s.installed,
                     !.reused = useShared],
                        !.copies[w] = s.installed], <<>>)}
       ELSE {}

Mutate(p, s, w) ==
    LET writer == s.writers[w]
    IN IF writer.phase = "open"
       THEN {Transition("mutate",
              [s EXCEPT !.writers[w].phase = "sealed",
                        !.copies[writer.slot] =
                          Patch(@, p.writeScope[w], p.writeValues[w])],
              <<Emit(p, w, "Sealed",
                     [writer |-> w, scope |-> p.writeScope[w],
                      bytes |-> Patch(s.copies[writer.slot],
                                      p.writeScope[w], p.writeValues[w])])>>)}
       ELSE {}

Install(p, s, w) ==
    LET writer == s.writers[w]
        commit == writer.decision = "commit"
        whole == p.mutant = "whole-page-install"
        result == IF commit
                  THEN IF whole THEN s.copies[writer.slot]
                       ELSE Patch(s.installed, p.writeScope[w], s.copies[writer.slot])
                  ELSE IF p.mutant = "whole-page-undo" THEN writer.before
                       ELSE s.installed
        restore == {m \in s.materials :
                       m.recipe = p.restoreRecipe /\
                       m.interpretation = p.restoreInterpretation}
        choices == IF ~commit /\ writer.reused THEN restore
                   ELSE {[bytes |-> Zero(p)]}
    IN IF writer.phase = "sealed" /\ writer.decision \in {"commit", "abort"}
       THEN {Transition("install",
              [s EXCEPT !.writers[w].phase = "done",
                        !.writers[w].installed = commit,
                        !.installed = result,
                        !.copies[writer.slot] =
                          IF ~commit /\ writer.reused /\ p.mutant # "skip-restore"
                          THEN material.bytes ELSE @,
                        !.gate = IF ~commit /\ writer.reused THEN TRUE ELSE @],
              <<Emit(p, w, "Installed",
                     [writer |-> w, outcome |-> writer.decision,
                      scope |-> p.writeScope[w]])>>)
              : material \in choices}
       ELSE {}

Restart(p, s) ==
    IF p.crash /\ ~s.restarted /\
       \E o \in p.operations : s.operations[o].phase \in {"active", "complete"}
    THEN {Transition("restart",
           [s EXCEPT !.restarted = TRUE, !.incarnation = 2,
                     !.registry.open = FALSE], <<>>)}
    ELSE {}

Rebind(p, s) ==
    IF p.rebind /\ s.restarted /\ ~s.rebound /\
       ((Users(s, "shared") = {} /\ s.registry.drained) \/ p.mutant = "rebind-live")
    THEN {Transition("rebind",
           [s EXCEPT !.rebound = TRUE, !.copyGeneration["shared"] = 2,
                     !.copies["shared"] = [i \in Items(p) |-> 90 + i]], <<>>)}
    ELSE {}

DrainBackend(p,s) ==
    {Transition(step.tag,[s EXCEPT !.registry = step.next],step.emissions)
     : step \in RegistryDrain(p,s.registry)}

HostReset(p,s) ==
    IF p.hostReset /\ ~s.hostReset /\
       \A v \in p.views : s.views[v].closed
    THEN {Transition("host-reset",
            [s EXCEPT !.registry = step.next, !.hostReset = TRUE,
                      !.incarnation = 2,
                      !.copies = [slot \in DOMAIN @ |-> Zero(p)],
                      !.operations = [o \in DOMAIN @ |->
                         IF @[o].phase \in {"queued","active"}
                         THEN [@[o] EXCEPT !.phase = "cancelled",!.cancelledByHost = TRUE]
                         ELSE @[o]]],<<>>)
          : step \in RegistryReset(s.registry)}
    ELSE {}

Actions(p, s) ==
    UNION {MapView(p,s,v) \cup ReadView(p,s,v) \cup CloseView(p,s,v) \cup
           CPUWriteAttempt(p,s,v) \cup
           UNION {ResolvePage(p,s,v,pg) : pg \in p.pages} : v \in p.views}
    \cup UNION {BeginBorrow(p,s,o) \cup Backend(p,s,o) \cup Cancel(p,s,o)
                : o \in p.operations}
    \cup UNION {BeginWriter(p,s,w) \cup Mutate(p,s,w) \cup Install(p,s,w)
                : w \in p.writers}
    \cup CloseGate(p,s) \cup Restart(p,s) \cup Rebind(p,s) \cup DrainBackend(p,s)
    \cup HostReset(p,s)

Terminal(p, s) ==
    /\ \A v \in p.views : s.views[v].closed
    /\ \A w \in p.writers : s.writers[w].phase = "done"
    /\ \A o \in p.operations : s.operations[o].phase \in {"retired", "refused"}
=============================================================================
