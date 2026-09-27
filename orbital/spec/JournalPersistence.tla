-------------------------- MODULE JournalPersistence ---------------------------
EXTENDS Contracts
CONSTANTS JournalNone, Mode, FoldCallbacks, Shape

\* Two acceptors can each crash with a submitted write still outstanding. This
\* local protocol does not know about quorums or chosen values. Its abstract
\* relation is deliberately permissive about future proposals, but forbids
\* promise reversal, same-ballot replacement, and invented reply snapshots.
Voters == IF Shape = "joint" THEN {"A", "B"} ELSE {"A"}
Disk == [promise |-> 0, ballot |-> 0, prefix |-> <<>>]
Request(kind, ballot, prefix) == [kind |-> kind, ballot |-> ballot, prefix |-> prefix]
Requests == {Request("promise", 2, <<>>), Request("accept", 1, <<1>>),
             Request("accept", 2, <<2>>)} \cup
            (IF Shape = "suffix" THEN {Request("accept", 1, <<1, 2>>)} ELSE {})
VARIABLES disk, pending, callbacks, replies, incarnation, reset, issued,
          history, refinement, witnessed
vars == <<disk, pending, callbacks, replies, incarnation, reset, issued,
          history, refinement, witnessed>>
Init == /\ disk = [v \in Voters |-> Disk]
        /\ pending = [v \in Voters |-> JournalNone]
        /\ callbacks = {} /\ replies = {}
        /\ incarnation = [v \in Voters |-> 0]
        /\ reset = {} /\ issued = {} /\ history = {}
        /\ refinement = TRUE /\ witnessed = FALSE

Allowed(d, r) ==
    /\ r.ballot >= d.promise
    /\ r.kind = "promise" \/ d.ballot # r.ballot \/ Prefix(d.prefix, r.prefix)

AbstractCompletion(old, new, r) ==
    /\ Allowed(old, r)
    /\ IF r.kind = "promise"
       THEN new = [old EXCEPT !.promise = r.ballot]
       ELSE new = [promise |-> r.ballot, ballot |-> r.ballot, prefix |-> r.prefix]

Submit(v, r) ==
    /\ <<v, r>> \notin issued
    /\ pending[v] = JournalNone
    /\ Allowed(disk[v], r)
    /\ pending' = [pending EXCEPT ![v] = [request |-> r, inc |-> incarnation[v]]]
    /\ issued' = issued \cup {<<v, r>>}
    /\ UNCHANGED <<disk, callbacks, replies, incarnation, reset, history, refinement, witnessed>>

Complete(v) ==
    /\ pending[v] # JournalNone
    /\ LET w == pending[v]
           r == w.request
           after == IF r.kind = "promise" THEN [disk[v] EXCEPT !.promise = r.ballot]
                    ELSE [promise |-> r.ballot, ballot |-> r.ballot, prefix |-> r.prefix]
           reply == [voter |-> v, kind |-> r.kind, ballot |-> r.ballot,
                     snapshot |-> after, inc |-> w.inc]
           fold == FoldCallbacks /\ w.inc = incarnation[v]
       IN /\ disk' = [disk EXCEPT ![v] = after]
          /\ pending' = [pending EXCEPT ![v] = JournalNone]
          /\ history' = history \cup {reply}
          /\ callbacks' = IF fold THEN callbacks ELSE callbacks \cup {reply}
          /\ replies' = IF fold THEN replies \cup {reply} ELSE replies
          /\ refinement' = (refinement /\ AbstractCompletion(disk[v], after, r))
    /\ witnessed' = (witnessed \/
          (reset = Voters /\ \A n \in Voters : pending[n] # JournalNone))
    /\ UNCHANGED <<incarnation, reset, issued>>

Callback(reply) ==
    /\ reply \in callbacks
    /\ callbacks' = callbacks \ {reply}
    /\ replies' = IF reply.inc = incarnation[reply.voter]
                  THEN replies \cup {reply} ELSE replies
    /\ UNCHANGED <<disk, pending, incarnation, reset, issued, history, refinement, witnessed>>

Reset(v) ==
    /\ v \notin reset /\ pending[v] # JournalNone
    /\ incarnation' = [incarnation EXCEPT ![v] = @ + 1]
    /\ reset' = reset \cup {v}
    /\ UNCHANGED <<disk, pending, callbacks, replies, issued, history, refinement, witnessed>>

\* A newer promise must not bypass an older write still owned by the device.
Overtake(v) ==
    /\ Mode = "overtake" /\ pending[v] # JournalNone
    /\ pending[v].request.ballot = 1 /\ disk[v].promise < 2
    /\ disk' = [disk EXCEPT ![v].promise = 2]
    /\ UNCHANGED <<pending, callbacks, replies, incarnation, reset, issued, history, refinement, witnessed>>

EarlyReply(v) ==
    /\ Mode = "early_reply" /\ pending[v] # JournalNone
    /\ LET w == pending[v]
           reply == [voter |-> v, kind |-> w.request.kind, ballot |-> w.request.ballot,
                     snapshot |-> disk[v], inc |-> w.inc]
       IN /\ reply \notin replies
          /\ replies' = replies \cup {reply}
    /\ UNCHANGED <<disk, pending, callbacks, incarnation, reset, issued, history, refinement, witnessed>>

Progress ==
    (\E v \in Voters : \E r \in Requests : Submit(v, r)) \/
    (\E v \in Voters : Complete(v) \/ Reset(v) \/ Overtake(v) \/ EarlyReply(v)) \/
    (\E reply \in callbacks : Callback(reply))
Done == /\ \A v \in Voters : pending[v] = JournalNone
        /\ callbacks = {}
        /\ \A v \in Voters : \A r \in Requests :
              <<v, r>> \in issued \/ ~Allowed(disk[v], r)
Next == Progress \/ (Done /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars
RefinesAcceptor == refinement
RepliesFromDurableSnapshots == replies \subseteq history
PromiseMonotone ==
    \A r \in history : disk[r.voter].promise >= r.ballot
NoJointAmbiguousCompletion == ~witnessed
=============================================================================
