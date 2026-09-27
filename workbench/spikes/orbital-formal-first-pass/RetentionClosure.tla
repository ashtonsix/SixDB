------------------------- MODULE RetentionClosure -------------------------
EXTENDS Integers, FiniteSets

(* A proposed SERIALIZED retention authority, not distributed GC. A caller
   requests one old view; durable registration precedes its grant. The authority
   excludes collection while registration's durable callback is pending and
   drains already-submitted root writes before rebuilding its registry after
   process restart. This drain is one PROPOSED physical service contract, not
   an existing generic Context port or distributed authority fence. An alternative
   could isolate namespaces/generations and recover valid old roots; not modelled.
   The drain request and completion are explicit; process death alone does not
   cancel a write. One authored registry write may be pending. Its old completion
   cannot run a new incarnation's callback. Surviving storage and drain availability
   after this process restart do not imply recovery after permanent storage loss.
   PreRegistered selects the smaller already-rooted native experiment fixture.

   One immutable symbolic chunk has versions 0,1,2. "base", delta tokens and
   "codec" represent actual dependencies, not proof supplied by a root name.
   An admitted suffix record advances head atomically; this imports atomic
   journal-record application, not an atomic checkpoint write+publication.
   The checkpoint write, durable completion and publication ARE separate.
   Replay's durable cursor includes the reconstructed value at that cursor.
   Code and physical leases are separate dependencies; BorrowLifetime owns
   backend stability. No byte implementation, distributed root transfer,
   unbounded writer stream or finite-resource progress claim is made here. *)

CONSTANTS Mode, PreRegistered
Modes == {"safe", "forget-replay", "drop-codec", "publish-early",
          "grant-early", "read-latest", "skip-drain"}
ASSUME /\ Mode \in Modes /\ PreRegistered \in BOOLEAN
Records == {"base", "d1", "d2", "codec", "cp2"}
Roots == {"reader", "replay"}
None == 3

VARIABLES store, head, checkpointPending, checkpointPublished, roots,
          replayCursor, replayRead, replayUnavailable, phase, incarnation,
          knownRoots, requested, granted, denied, readerReleased, readResult,
          gcSeen, registrationPending, registrationIncarnation
vars == <<store, head, checkpointPending, checkpointPublished, roots,
          replayCursor, replayRead, replayUnavailable, phase, incarnation,
          knownRoots, requested, granted, denied, readerReleased, readResult,
          gcSeen, registrationPending, registrationIncarnation>>

Chain(v) == {"base", "codec"} \cup
            (IF v >= 1 THEN {"d1"} ELSE {}) \cup
            (IF v >= 2 THEN {"d2"} ELSE {})
HeadNeeds == IF checkpointPublished THEN {"cp2", "codec"} ELSE Chain(head)
ReplayNeeds == {"codec"} \cup
              (IF replayCursor < 0 THEN {"base"} ELSE {}) \cup
              (IF replayCursor < 1 THEN {"d1"} ELSE {}) \cup
              (IF replayCursor < 2 /\ head = 2 THEN {"d2"} ELSE {})
Needs(r) == IF r = "reader" THEN Chain(1) ELSE ReplayNeeds
EffectiveRoots == IF Mode = "forget-replay" THEN knownRoots \ {"replay"}
                  ELSE knownRoots
Keep == HeadNeeds \cup UNION {Needs(r) : r \in EffectiveRoots}

Init ==
  /\ store = Chain(1) /\ head = 1
  /\ checkpointPending = FALSE /\ checkpointPublished = FALSE
  /\ roots = IF PreRegistered THEN Roots ELSE {"replay"}
  /\ knownRoots = roots
  /\ replayCursor = -1 /\ replayRead = None /\ replayUnavailable = FALSE
  /\ phase = "serving" /\ incarnation = 1
  /\ requested = PreRegistered /\ granted = PreRegistered
  /\ denied = FALSE /\ readerReleased = FALSE /\ readResult = "none"
  /\ gcSeen = FALSE /\ registrationPending = FALSE
  /\ registrationIncarnation = 0

RequestView ==
  /\ ~requested /\ ~PreRegistered /\ requested' = TRUE
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       knownRoots, granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

BeginRegister ==
  /\ phase = "serving" /\ requested /\ ~denied /\ ~readerReleased
  /\ "reader" \notin roots /\ Chain(1) \subseteq store
  /\ ~registrationPending
  /\ phase' = "registering" /\ registrationPending' = TRUE
  /\ registrationIncarnation' = incarnation
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, incarnation, knownRoots,
       requested, granted, denied, readerReleased, readResult, gcSeen>>

PersistRegister ==
  /\ registrationPending /\ registrationPending' = FALSE
  /\ roots' = roots \cup {"reader"}
  /\ phase' = IF phase = "registering" /\ registrationIncarnation = incarnation
                THEN "registered" ELSE phase
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished,
       replayCursor, replayRead, replayUnavailable, incarnation, knownRoots,
       requested, granted, denied, readerReleased, readResult, gcSeen, registrationIncarnation>>

RegistrationCallback ==
  /\ phase = "registered" /\ phase' = "serving" /\ knownRoots' = roots
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, incarnation, requested,
       granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

Grant ==
  /\ ~granted /\ ~readerReleased /\ requested /\ ~denied
  /\ ((phase = "serving" /\ "reader" \in knownRoots)
       \/ (Mode = "grant-early" /\ phase = "registering"))
  /\ granted' = TRUE
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       knownRoots, requested, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

DenyUnavailableView ==
  /\ phase = "serving" /\ requested /\ ~denied /\ ~readerReleased
  /\ "reader" \notin roots /\ ~(Chain(1) \subseteq store)
  /\ denied' = TRUE
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       knownRoots, requested, granted, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

Append ==
  /\ phase = "serving" /\ head = 1
  /\ store' = store \cup {"d2"} /\ head' = 2
  /\ UNCHANGED <<checkpointPending, checkpointPublished, roots, replayCursor,
       replayRead, replayUnavailable, phase, incarnation, knownRoots, requested,
       granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

BeginCheckpoint ==
  /\ phase = "serving" /\ head = 2 /\ ~checkpointPending
  /\ ~checkpointPublished /\ "cp2" \notin store /\ Chain(2) \subseteq store
  /\ checkpointPending' = TRUE
  /\ UNCHANGED <<store, head, checkpointPublished, roots, replayCursor,
       replayRead, replayUnavailable, phase, incarnation, knownRoots, requested,
       granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

PersistCheckpoint ==
  /\ checkpointPending /\ checkpointPending' = FALSE
  /\ store' = store \cup {"cp2"}
  /\ UNCHANGED <<head, checkpointPublished, roots, replayCursor, replayRead,
       replayUnavailable, phase, incarnation, knownRoots, requested, granted,
       denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

PublishCheckpoint ==
  /\ phase = "serving" /\ head = 2 /\ ~checkpointPublished
  /\ ("cp2" \in store \/ (Mode = "publish-early" /\ checkpointPending))
  /\ checkpointPublished' = TRUE
  /\ UNCHANGED <<store, head, checkpointPending, roots, replayCursor,
       replayRead, replayUnavailable, phase, incarnation, knownRoots, requested,
       granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

Collect(record) ==
  /\ phase = "serving" /\ record \in store
  /\ (record \notin Keep \/ (Mode = "drop-codec" /\ record = "codec"))
  /\ store' = store \ {record}
  /\ gcSeen' = (gcSeen \/ (checkpointPublished /\ incarnation = 2))
  /\ UNCHANGED <<head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       knownRoots, requested, granted, denied, readerReleased, readResult, registrationPending, registrationIncarnation>>

InspectCollection ==
  /\ phase = "serving" /\ checkpointPublished /\ incarnation = 2
  /\ ~gcSeen /\ gcSeen' = TRUE
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       knownRoots, requested, granted, denied, readerReleased, readResult, registrationPending, registrationIncarnation>>

ReadOld ==
  /\ phase = "serving" /\ granted /\ ~readerReleased /\ readResult = "none"
  /\ incarnation = 2 /\ head = 2 /\ checkpointPublished /\ gcSeen
  /\ readResult' = IF ~(Chain(1) \subseteq store) THEN "missing"
                    ELSE IF Mode = "read-latest" THEN "v2" ELSE "v1"
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       knownRoots, requested, granted, denied, readerReleased, gcSeen, registrationPending, registrationIncarnation>>

ReleaseReader ==
  /\ phase = "serving" /\ readResult = "v1" /\ ~readerReleased
  /\ readerReleased' = TRUE /\ granted' = FALSE
  /\ roots' = roots \ {"reader"} /\ knownRoots' = knownRoots \ {"reader"}
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       requested, denied, readResult, gcSeen, registrationPending, registrationIncarnation>>

ReplayRequest ==
  /\ phase = "serving" /\ "replay" \in roots /\ replayRead = None
  /\ replayCursor < head /\ ~replayUnavailable
  /\ LET record == CASE replayCursor = -1 -> "base"
                         [] replayCursor = 0 -> "d1"
                         [] OTHER -> "d2"
     IN /\ replayUnavailable' = ~({record, "codec"} \subseteq store)
        /\ replayRead' = replayCursor + 1
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, phase, incarnation, knownRoots, requested, granted, denied,
       readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

PersistReplayCursor ==
  /\ phase = "serving" /\ replayRead # None
  /\ replayRead = replayCursor + 1 /\ ~replayUnavailable
  /\ replayCursor' = replayRead /\ replayRead' = None
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayUnavailable, phase, incarnation, knownRoots, requested, granted,
       denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

ReleaseReplay ==
  /\ phase = "serving" /\ replayCursor = 2 /\ replayRead = None
  /\ "replay" \in roots
  /\ roots' = roots \ {"replay"} /\ knownRoots' = knownRoots \ {"replay"}
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished,
       replayCursor, replayRead, replayUnavailable, phase, incarnation,
       requested, granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

Crash ==
  /\ incarnation = 1 /\ phase # "down"
  /\ phase' = "down" /\ knownRoots' = {} /\ replayRead' = None
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayUnavailable, incarnation, requested, granted, denied,
       readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

Restart ==
  /\ phase = "down" /\ incarnation = 1
  /\ phase' = "draining" /\ incarnation' = 2
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, knownRoots, requested,
       granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

(* The physical service acknowledges drain only after the outstanding old
   root write is retired. The new incarnation cannot infer this from silence. *)
FinishRootDrain ==
  /\ phase = "draining"
  /\ (~registrationPending \/ Mode = "skip-drain")
  /\ phase' = "recovering"
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, incarnation, knownRoots,
       requested, granted, denied, readerReleased, readResult, gcSeen,
       registrationPending, registrationIncarnation>>

RecoverRegistry ==
  /\ phase = "recovering" /\ phase' = "serving" /\ knownRoots' = roots
  /\ UNCHANGED <<store, head, checkpointPending, checkpointPublished, roots,
       replayCursor, replayRead, replayUnavailable, incarnation, requested,
       granted, denied, readerReleased, readResult, gcSeen, registrationPending, registrationIncarnation>>

Terminal ==
  /\ phase = "serving" /\ ~checkpointPending /\ replayRead = None
  /\ roots = {} /\ ~granted /\ checkpointPublished
  /\ (readerReleased \/ denied \/ ~requested)
  /\ store = {"cp2", "codec"}

Next == RequestView \/ BeginRegister \/ PersistRegister \/ RegistrationCallback
     \/ Grant \/ DenyUnavailableView \/ Append \/ BeginCheckpoint
     \/ PersistCheckpoint \/ PublishCheckpoint \/ (\E r \in Records : Collect(r))
     \/ InspectCollection \/ ReadOld \/ ReleaseReader \/ ReplayRequest
     \/ PersistReplayCursor \/ ReleaseReplay \/ Crash \/ Restart
     \/ FinishRootDrain \/ RecoverRegistry \/ (Terminal /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ store \subseteq Records /\ head \in 1..2 /\ roots \subseteq Roots
  /\ checkpointPending \in BOOLEAN /\ checkpointPublished \in BOOLEAN
  /\ replayCursor \in -1..2 /\ replayRead \in 0..3
  /\ replayUnavailable \in BOOLEAN /\ incarnation \in 1..2
  /\ phase \in {"serving", "registering", "registered", "down", "draining", "recovering"}
  /\ registrationPending \in BOOLEAN /\ registrationIncarnation \in 0..2
  /\ knownRoots \subseteq Roots /\ requested \in BOOLEAN /\ granted \in BOOLEAN
  /\ denied \in BOOLEAN /\ readerReleased \in BOOLEAN /\ gcSeen \in BOOLEAN
  /\ readResult \in {"none", "v1", "v2", "missing"}
HeadReconstructible == HeadNeeds \subseteq store
LiveRootsReconstructible == \A r \in roots : Needs(r) \subseteq store
GrantHasDurableRoot == granted => "reader" \in roots
VersionBound == readResult \in {"none", "v1"}
ReplayReadable == ~replayUnavailable
ServingRegistryCurrent == phase = "serving" => knownRoots = roots
NoRecoveryWitness == ~(Terminal /\ readerReleased /\ readResult = "v1" /\
                       replayCursor = 2 /\ incarnation = 2)
NoDurableRegistrationCrash == ~(phase = "recovering" /\ "reader" \in roots /\
                               requested /\ ~granted /\ ~PreRegistered)
NoDurableCheckpointCrash == ~(phase = "down" /\ "cp2" \in store /\
                             ~checkpointPublished)
=============================================================================
