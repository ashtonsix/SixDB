------------------------- MODULE CheckedReplay -------------------------
EXTENDS Naturals, FiniteSets

(* One shard; its agreed context and root registration are one durable    *)
(* authority record. This is a proposed serialized grant protocol, not a *)
(* distributed-GC or consensus proof. Tokens stand for actual immutable  *)
(* base/decoder contents. Reconstruct must find them in durable storage. *)
(* Source advancement follows admission of this old read. There is one  *)
(* optional process reset; storage destruction is outside this model.    *)
CONSTANTS NumCheckers, HasEffect, Bug, None
Checkers == 1..NumCheckers
Recipe(v) == IF v = 0 THEN {"base0", "code0"} ELSE {"base1", "code1"}
Value(v) == IF v = 0 THEN 5 ELSE 9
Context == [id |-> "read-0", cut |-> 0, profile |-> "profile-0",
            checkers |-> Checkers, effects |-> IF HasEffect THEN {"out"} ELSE {}]
ForeignContext == [Context EXCEPT !.id = "prior-invocation", !.profile = "profile-old"]

VARIABLES disk, head, rootRecord, rootLive, localContext,
          loaded, privateReport, durableReport, received,
          decision, knownDecision, published, result,
          crashed, recovered, otherWork, witness
vars == <<disk, head, rootRecord, rootLive, localContext, loaded,
          privateReport, durableReport, received, decision, knownDecision,
          published, result, crashed, recovered, otherWork, witness>>

Init ==
  /\ disk = Recipe(0)
  /\ head = 0
  /\ rootRecord = None
  /\ rootLive = FALSE
  /\ localContext = None
  /\ loaded = [k \in Checkers |-> None]
  /\ privateReport = [k \in Checkers |-> None]
  /\ durableReport = [k \in Checkers |-> None]
  /\ received = {}
  /\ decision = None
  /\ knownDecision = None
  /\ published = FALSE
  /\ result = None
  /\ crashed = FALSE
  /\ recovered = FALSE
  /\ otherWork = 0
  /\ witness = [beforeGrant |-> FALSE, afterDecision |-> FALSE, rebuilt |-> FALSE]

Register ==
  /\ rootRecord = None
  /\ rootRecord' = Context
  /\ rootLive' = TRUE
  /\ UNCHANGED <<disk, head, localContext, loaded, privateReport,
       durableReport, received, decision, knownDecision, published, result,
       crashed, recovered, otherWork, witness>>

(* Delivery of the durable registration receipt, or reading the same     *)
(* record after restart. The context is obtained from storage, not Init. *)
ReadGrant ==
  /\ rootRecord # None
  /\ localContext = None
  /\ ~(Bug = "lost-recovery" /\ crashed)
  /\ localContext' = rootRecord
  /\ recovered' = (recovered \/ crashed)
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, loaded, privateReport,
       durableReport, received, decision, knownDecision, published, result,
       crashed, otherWork, witness>>

Advance ==
  /\ rootRecord # None
  /\ head = 0
  /\ disk' = disk \cup Recipe(1)
  /\ head' = 1
  /\ UNCHANGED <<rootRecord, rootLive, localContext, loaded, privateReport,
       durableReport, received, decision, knownDecision, published, result,
       crashed, recovered, otherWork, witness>>

Reconstruct(k) ==
  /\ localContext # None
  /\ rootLive
  /\ loaded[k] = None
  /\ durableReport[k] = None
  /\ LET cut == IF Bug = "current-cut" THEN head ELSE localContext.cut
     IN /\ Recipe(cut) \subseteq disk
        /\ loaded' = [loaded EXCEPT ![k] =
             [context |-> localContext, cut |-> cut, value |-> Value(cut)]]
  /\ witness' = [witness EXCEPT !.rebuilt = (@ \/ (crashed /\ recovered /\ head = 1))]
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext,
       privateReport, durableReport, received, decision, knownDecision,
       published, result, crashed, recovered, otherWork>>

(* Two complete interactions return the same final result. The request  *)
(* identity differs; final-result-only checking must detect its omission.*)
Execute(k) ==
  /\ loaded[k] # None
  /\ privateReport[k] = None
  /\ \E request \in {"request-a", "request-b"} :
       privateReport' = [privateReport EXCEPT ![k] =
         [context |-> loaded[k].context,
          transcript |-> <<request, loaded[k].value,
                          loaded[k].context.effects, "completed">>,
          result |-> 7]]
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       durableReport, received, decision, knownDecision, published, result,
       crashed, recovered, otherWork, witness>>

PersistReport(k) ==
  /\ privateReport[k] # None
  /\ durableReport[k] = None
  /\ durableReport' = [durableReport EXCEPT ![k] = privateReport[k]]
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, received, decision, knownDecision, published, result,
       crashed, recovered, otherWork, witness>>

(* A delayed previously recorded report from the same checker identity.
   Its bytes are authentic for another invocation. Binding it to this one is
   precisely what the protocol must reject, even if all checkers agree. *)
ReplayPriorReport(k) ==
  /\ rootRecord # None
  /\ durableReport[k] = None
  /\ durableReport' = [durableReport EXCEPT ![k] =
       [context |-> ForeignContext,
        transcript |-> <<"request-a", 5, ForeignContext.effects, "completed">>,
        result |-> 7]]
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, received, decision, knownDecision, published, result,
       crashed, recovered, otherWork, witness>>

DeliverReport(k) ==
  /\ localContext # None
  /\ durableReport[k] # None
  /\ k \notin received
  /\ received' = received \cup {k}
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, durableReport, decision, knownDecision, published,
       result, crashed, recovered, otherWork, witness>>

MatchingReports ==
  /\ Bug = "unbound-context" \/ (\A k \in received : durableReport[k].context = localContext)
  /\ \A a, b \in received :
       IF Bug = "final-only"
       THEN durableReport[a].result = durableReport[b].result
       ELSE IF Bug = "unbound-context"
            THEN /\ durableReport[a].transcript = durableReport[b].transcript
                 /\ durableReport[a].result = durableReport[b].result
            ELSE durableReport[a] = durableReport[b]

PersistDecision ==
  /\ decision = None
  /\ localContext # None
  /\ IF Bug = "missing-checker" THEN received # {} ELSE received = Checkers
  /\ decision' = [context |-> localContext,
                    evidence |-> [k \in received |-> durableReport[k]],
                    outcome |-> IF MatchingReports /\ Bug # "always-abort" THEN "commit" ELSE "abort",
                    accepted |-> IF MatchingReports /\ Bug # "always-abort"
                                 THEN durableReport[CHOOSE k \in received : TRUE] ELSE None]
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, durableReport, received, knownDecision, published,
       result, crashed, recovered, otherWork, witness>>

ReadDecision ==
  /\ decision # None
  /\ knownDecision = None
  /\ localContext # None
  /\ knownDecision' = decision
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, durableReport, received, decision, published, result,
       crashed, recovered, otherWork, witness>>

Publish ==
  /\ knownDecision # None
  /\ ~published
  /\ published' = TRUE
  /\ result' = IF Bug = "wrong-result" /\ knownDecision.outcome = "commit"
               THEN [knownDecision EXCEPT !.accepted.result = 8] ELSE knownDecision
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, durableReport, received, decision, knownDecision,
       crashed, recovered, otherWork, witness>>

(* This model closes its explicit replay/audit lease after observation.  *)
(* It does not select a production audit-retention duration.             *)
Close ==
  /\ published
  /\ rootLive
  /\ rootLive' = FALSE
  /\ UNCHANGED <<disk, head, rootRecord, localContext, loaded, privateReport,
       durableReport, received, decision, knownDecision, published, result,
       crashed, recovered, otherWork, witness>>

Collect(token) ==
  /\ token \in disk \ Recipe(head)
  /\ ~rootLive \/ Bug = "forget-root"
  /\ disk' = disk \ {token}
  /\ UNCHANGED <<head, rootRecord, rootLive, localContext, loaded,
       privateReport, durableReport, received, decision, knownDecision,
       published, result, crashed, recovered, otherWork, witness>>

Crash ==
  /\ rootRecord # None
  /\ ~crashed
  /\ ~published
  /\ witness' = [witness EXCEPT
       !.beforeGrant = (localContext = None /\ decision = None),
       !.afterDecision = (decision # None /\ knownDecision = None)]
  /\ crashed' = TRUE
  /\ localContext' = None
  /\ loaded' = [k \in Checkers |-> None]
  /\ privateReport' = [k \in Checkers |-> None]
  /\ received' = {}
  /\ knownDecision' = None
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, durableReport, decision,
       published, result, recovered, otherWork>>

(* The lost-recovery control leaves an independent service runnable, so *)
(* TLC must detect the stalled transaction, not just a total deadlock.   *)
OtherService ==
  /\ Bug = "lost-recovery"
  /\ otherWork' = 1 - otherWork
  /\ UNCHANGED <<disk, head, rootRecord, rootLive, localContext, loaded,
       privateReport, durableReport, received, decision, knownDecision,
       published, result, crashed, recovered, witness>>

Terminal ==
  /\ published /\ ~rootLive /\ head = 1 /\ disk = Recipe(1)
  /\ UNCHANGED vars

Next == Register \/ ReadGrant \/ Advance \/ PersistDecision \/ ReadDecision
        \/ Publish \/ Close \/ Crash \/ OtherService \/ Terminal
        \/ (\E k \in Checkers : Reconstruct(k) \/ Execute(k)
                               \/ PersistReport(k) \/ DeliverReport(k) \/ ReplayPriorReport(k))
        \/ (\E token \in {"base0", "base1", "code0", "code1"} : Collect(token))
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Register) /\ WF_vars(ReadGrant)
  /\ WF_vars(Advance) /\ WF_vars(PersistDecision) /\ WF_vars(ReadDecision)
  /\ WF_vars(Publish) /\ WF_vars(Close)
  /\ \A k \in Checkers : WF_vars(Reconstruct(k)) /\ WF_vars(Execute(k))
       /\ WF_vars(PersistReport(k)) /\ WF_vars(DeliverReport(k))

RootRecoverable == rootLive => Recipe(rootRecord.cut) \subseteq disk
ReadsAtCut == \A k \in Checkers : loaded[k] # None =>
  /\ loaded[k].context = rootRecord
  /\ loaded[k].cut = rootRecord.cut
  /\ loaded[k].value = Value(rootRecord.cut)
AllEvidence == decision # None /\ decision.outcome = "commit" =>
  /\ \A k \in Checkers : durableReport[k] # None
  /\ \A k \in Checkers : durableReport[k].context = rootRecord
  /\ \A a, b \in Checkers : durableReport[a] = durableReport[b]
EvidenceMatches(d) ==
  /\ DOMAIN d.evidence = rootRecord.checkers
  /\ \A k \in DOMAIN d.evidence : d.evidence[k].context = rootRecord
  /\ \A a, b \in DOMAIN d.evidence : d.evidence[a] = d.evidence[b]
DecisionMeaning == decision # None =>
  /\ decision.context = rootRecord
  /\ decision.outcome = (IF EvidenceMatches(decision) THEN "commit" ELSE "abort")
  /\ IF decision.outcome = "commit"
       THEN \A k \in Checkers : decision.accepted = decision.evidence[k]
       ELSE decision.accepted = None
PublicationEvidence == published => result = decision /\ result.context = rootRecord
TypeOK == /\ disk \subseteq {"base0", "code0", "base1", "code1"}
          /\ head \in {0, 1} /\ received \subseteq Checkers
          /\ rootLive \in BOOLEAN /\ published \in BOOLEAN
          /\ crashed \in BOOLEAN /\ recovered \in BOOLEAN
          /\ otherWork \in {0, 1}
Completes == <>published
NotRecoveredPublication == ~(published /\ result.outcome = "commit" /\
  witness.beforeGrant /\ witness.rebuilt /\ recovered /\ head = 1)
NotDecisionRecovery == ~(published /\ result.outcome = "commit" /\ witness.afterDecision /\ recovered)
NotCommit == ~(published /\ result.outcome = "commit" /\ result.accepted.result = 7)
NotRetired == ~(published /\ ~rootLive /\ head = 1 /\ disk = Recipe(1))
NotAbort == ~(published /\ result.outcome = "abort")
=======================================================================
