------------------------------ MODULE DurableLog ------------------------------
EXTENDS Contracts

\* Abstract service. JournalAuthority checks the concrete provider's trace
\* projection. A submission is not a chosen record; local delivery is separate.
\* params: owners, actors, subscribers[owner], initialConfig[owner].
Init(p) ==
    [log |-> [j \in p.owners |-> <<>>],
     config |-> p.initialConfig,
     pending |-> {}, seen |-> {}, recoveries |-> {},
     delivered |-> [a \in p.actors |-> [j \in p.owners |-> 0]]]

Receive(p, s, e) ==
    IF e.kind = "journal.submit" /\ e.body.owner \in p.owners
    THEN {Transition("log.submit",
              [s EXCEPT !.pending = @ \cup {e}], <<>>)}
    ELSE IF e.kind = "journal.recover" /\ e.src \in p.actors
    THEN LET command == Command(e.dst, <<"recover", e.id>>, "journal.barrier",
                             [actor |-> e.src, recovery |-> e.id])
             request == Event(<<"barrier", e.id>>, e.src, e.dst,
                              "journal.submit", command)
             query == [owner |-> e.dst, actor |-> e.src, request |-> e.id]
         IN {Transition("log.recover",
              [s EXCEPT !.pending = @ \cup {request},
                        !.recoveries = @ \cup {query}], <<>>)}
    ELSE {}

AppendOne(p, s, e) ==
    LET j == e.body.owner
        c == e.body
        nextConfig == IF c.kind = "journal.handoff"
                      THEN [s.config EXCEPT ![j] = c.body.target]
                      ELSE s.config
    IN Transition("log.choose",
         [s EXCEPT !.log[j] = Append(@, c),
                   !.config = nextConfig,
                   !.pending = @ \ {e},
                   !.seen = @ \cup {e.id}], <<>>)

DeliverOne(p, s, a, j) ==
    LET i == s.delivered[a][j] + 1
        body == [owner |-> j, index |-> i, command |-> s.log[j][i],
                 config |-> s.config[j]]
    IN Transition("log.deliver",
         [s EXCEPT !.delivered[a][j] = i],
         <<Event(<<j, i, a>>, j, a, "journal.deliver", body)>>)

SnapshotOne(p, s, r, i) ==
    LET j == r.owner
        prefix == SubSeq(s.log[j], 1, i)
        body == [owner |-> j, config |-> s.config[j], index |-> i,
                 prefix |-> prefix, recovery |-> r.request]
    IN Transition("log.snapshot",
         [s EXCEPT !.recoveries = @ \ {r},
                   !.delivered[r.actor][j] = i],
         <<Event(<<"snapshot", j, r.actor, r.request>>, j, r.actor,
                 "journal.snapshot", body)>>)

Actions(p, s) ==
    {AppendOne(p, s, e) : e \in {x \in s.pending : x.id \notin s.seen}}
    \cup
    {Transition("log.duplicate",
        [s EXCEPT !.pending = @ \ {e}], <<>>) :
          e \in {x \in s.pending : x.id \in s.seen}}
    \cup
    UNION {{DeliverOne(p, s, a, j) :
                a \in {x \in p.subscribers[j] :
                        s.delivered[x][j] < Len(s.log[j]) /\
                        ~(\E r \in s.recoveries :
                                  r.owner = j /\ r.actor = x)}} : j \in p.owners}
    \cup UNION {{SnapshotOne(p, s, r, i) :
                  i \in {k \in 1..Len(s.log[r.owner]) :
                    s.log[r.owner][k].kind = "journal.barrier" /\
                    s.log[r.owner][k].body.recovery = r.request /\
                    s.log[r.owner][k].body.actor = r.actor}} :
                r \in s.recoveries}

\* The abstract transition relation used by the provider mapping. Learning can
\* reveal a whole prefix at once; a replay after restart can repeat old records.
PrefixExtension(old, new) ==
    DOMAIN old = DOMAIN new /\ \A j \in DOMAIN old : Prefix(old[j], new[j])

DeliverySound(log, e) ==
    IF e.kind = "journal.deliver"
    THEN /\ e.body.owner \in DOMAIN log
         /\ e.body.index \in 1..Len(log[e.body.owner])
         /\ e.body.command = log[e.body.owner][e.body.index]
    ELSE IF e.kind = "journal.snapshot"
    THEN /\ e.body.owner \in DOMAIN log
         /\ e.body.index = Len(e.body.prefix)
         /\ Prefix(e.body.prefix, log[e.body.owner])
         /\ e.body.index > 0
         /\ LET barrier == e.body.prefix[e.body.index]
            IN /\ barrier.kind = "journal.barrier"
               /\ barrier.body.actor = e.dst
               /\ barrier.body.recovery = e.body.recovery
    ELSE TRUE

\* Observable prefix contract. A concrete quorum acceptance can choose several
\* previously submitted records together. The one-record executor above is one
\* implementation; consumers receive ordered records, never an atomic global flag.
KnownCut(outputs, actor, owner) ==
    LET cuts == {0} \cup {e.body.index : e \in {x \in outputs :
           x.kind \in {"journal.deliver", "journal.snapshot"} /\
           ToString(x.dst) = ToString(actor) /\ x.body.owner = owner}}
    IN CHOOSE n \in cuts : \A m \in cuts : n >= m

ContractStep(p, oldLog, newLog, oldCommands, newCommands, oldOutput, newOutput) ==
    /\ PrefixExtension(oldLog, newLog)
    /\ oldCommands \subseteq newCommands
    /\ oldOutput \subseteq newOutput
    /\ \A j \in p.owners : Elements(newLog[j]) \subseteq newCommands
    /\ \A e \in newOutput : DeliverySound(newLog, e)
    /\ \A e \in newOutput \ oldOutput :
          e.kind = "journal.deliver" =>
             e.body.index <= KnownCut(oldOutput, e.dst, e.body.owner) + 1

Contract(p, logs, commands, outputs) ==
    /\ logs = [j \in p.owners |-> <<>>]
    /\ outputs = {}
    /\ [][ContractStep(p, logs, logs', commands, commands', outputs, outputs')]_
             <<logs, commands, outputs>>

=============================================================================
