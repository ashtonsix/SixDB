----------------------------- MODULE JournalKernel ----------------------------
EXTENDS Contracts
CONSTANT JournalNone

\* p: owners, actors, subscribers, initialConfig, configs, members[cfg],
\* owner[cfg], leaders[cfg][ballot], ballots, successors[cfg], mode.
\* Voter names include storage incarnation; process incarnations are separate.
\* Network sets retain an idempotent retransmission obligation, not packet counts.
\* A pending disk command survives process death and serializes all later commands.

Voters(p) == UNION {p.members[c] : c \in p.configs}
Leader(p, c, b) == p.leaders[c][b]
EmptyVote == [promise |-> 0, ab |-> 0, base |-> JournalNone, tail |-> <<>>,
              initialized |-> FALSE]
EmptyLeader == [phase |-> "idle", replies |-> {}, seq |-> <<>>]
EmptyCert(c) == [cfg |-> c, ballot |-> 0, seq |-> <<>>, votes |-> {}]
Vote(c, b, v, seq) == [cfg |-> c, ballot |-> b, voter |-> v, seq |-> seq]
Message(kind, c, b, from, to, body) ==
    [src |-> from, dst |-> to, kind |-> kind,
     body |-> [cfg |-> c, ballot |-> b, value |-> body]]

Init(p) ==
  [disk |-> [c \in p.configs |-> [v \in p.members[c] |->
       [EmptyVote EXCEPT !.initialized = c = p.initialConfig[p.owner[c]]]]],
   pending |-> [c \in p.configs |-> [v \in p.members[c] |-> JournalNone]],
   leaders |-> [c \in p.configs |-> [b \in p.ballots |-> EmptyLeader]],
   requests |-> [c \in p.configs |-> [v \in p.members[c] |-> {}]],
   hints |-> [c \in p.configs |-> [v \in p.members[c] |-> 0]],
   knowledge |-> [c \in p.configs |-> [v \in p.members[c] |-> {}]],
   learned |-> [c \in p.configs |-> [v \in p.members[c] |-> EmptyCert(c)]],
   inc |-> [v \in Voters(p) |-> 0],
   destroyed |-> {}, callbacks |-> {}, net |-> {}, blobs |-> {},
   submitted |-> {}, recoveries |-> {}, proposed |-> {},
   handoffs |-> [c \in p.configs |-> {}],
   delivered |-> [a \in p.actors |-> [j \in p.owners |-> 0]],
   acceptedHistory |-> {}, chosenHistory |-> {}, proposalHistory |-> {},
   outputHistory |-> {}, resetHistory |-> {}, compactHistory |-> {}]

BasePresent(s, d) == d.base = JournalNone \/ \E x \in s.blobs : x.id = d.base
BaseBytes(s, d) ==
    IF d.base = JournalNone THEN <<>>
    ELSE IF \E x \in s.blobs : x.id = d.base
         THEN (CHOOSE x \in s.blobs : x.id = d.base).seq
         ELSE <<>>
Full(s, c, v) == BaseBytes(s, s.disk[c][v]) \o s.disk[c][v].tail
Usable(p, s, c, v) ==
    /\ v \notin s.destroyed
    /\ s.disk[c][v].initialized
    /\ BasePresent(s, s.disk[c][v])

Stops(c, seq) ==
    {i \in 1..Len(seq) :
       seq[i].kind = "journal.handoff" /\ seq[i].body.source = c}
Terminal(c, seq) == Stops(c, seq) # {}
IsTerminalLast(c, seq) == \A i \in Stops(c, seq) : i = Len(seq)
HasCommand(seq, command) == \E i \in 1..Len(seq) : seq[i].id = command.id

MatchingVotes(p, votes, c, b, seq) ==
    {r \in votes : r.cfg = c /\ r.ballot = b /\
                    r.voter \in p.members[c] /\ Prefix(seq, r.seq)}
Quorum(p, votes, c, b, seq) ==
    Cardinality({r.voter : r \in MatchingVotes(p, votes, c, b, seq)}) * 2
       > Cardinality(p.members[c])
ValidCert(p, cert) ==
    /\ cert.cfg \in p.configs
    /\ Len(cert.seq) > 0
    /\ IF cert.votes = JournalNone THEN cert.ballot \in p.ballots
       ELSE Quorum(p, cert.votes, cert.cfg, cert.ballot, cert.seq)

NewChosen(p, history) ==
    UNION {{[cfg |-> r.cfg, ballot |-> r.ballot, seq |-> seq] :
        seq \in {q \in Prefixes(r.seq) :
            Len(q) > 0 /\ Quorum(p, history, r.cfg, r.ballot, q)}} : r \in history}

ChosenLog(p, s) ==
    [j \in p.owners |->
      LET candidates == {q.seq : q \in s.chosenHistory
                                   \cap {q \in s.chosenHistory :
                                           p.owner[q.cfg] = j}} \cup {<<>>}
      IN CHOOSE seq \in candidates :
                       \A other \in candidates : Len(seq) >= Len(other)]

Receive(p, s, e) ==
    IF e.kind = "journal.submit" /\ e.body.owner \in p.owners
    THEN LET c == p.initialConfig[e.body.owner]
             receivers == IF "ingress" \in DOMAIN p THEN p.ingress[e.body.owner] ELSE {Leader(p,c,1)}
         IN {Transition("journal.ingress",
               [s EXCEPT !.requests[c][v] = @ \cup {e.body},
                         !.submitted = @ \cup {e}], <<>>) : v \in receivers \ s.destroyed}
    ELSE IF e.kind = "journal.recover" /\ e.src \in p.actors /\
            e.dst \in p.owners
    THEN LET c == p.initialConfig[e.dst]
             receivers == IF "ingress" \in DOMAIN p THEN p.ingress[e.dst] ELSE {Leader(p,c,1)}
             command == Command(e.dst, <<"recover", e.id>>, "journal.barrier",
                            [actor |-> e.src, recovery |-> e.id])
             query == [owner |-> e.dst, actor |-> e.src, request |-> e.id]
         IN {Transition("journal.recover",
             [s EXCEPT !.requests[c][v] = @ \cup {command},
                       !.recoveries = @ \cup {query},
                       !.submitted = @ \cup {e}], <<>>) : v \in receivers \ s.destroyed}
    ELSE IF e.kind = "journal.handoff.request" /\ e.body.cfg \in p.configs
    THEN {Transition("journal.handoff.request",
             [s EXCEPT !.handoffs[e.body.cfg] = @ \cup {e.body.target}], <<>>)}
    ELSE {}

CompleteWrite(p, s, c, v) ==
    LET w == s.pending[c][v]
    IN IF w = JournalNone \/ v \in s.destroyed THEN {}
       ELSE IF w.kind = "promise"
       THEN {Transition("journal.promise.persist",
              [s EXCEPT !.disk[c][v].promise = w.ballot,
                        !.pending[c][v] = JournalNone,
                        !.callbacks = @ \cup
                          {[w EXCEPT !.seq = Full(s, c, v),
                              !.cert = s.disk[c][v].ab]}], <<>>)}
       ELSE IF w.kind = "accept"
       THEN LET vote == Vote(c, w.ballot, v, w.seq)
                hist == {old \in s.acceptedHistory :
                    ~(old.cfg = c /\ old.ballot = w.ballot /\ old.voter = v /\
                      Prefix(old.seq, w.seq))} \cup {vote}
            IN {Transition("journal.accept.persist",
               [s EXCEPT !.disk[c][v].promise = w.ballot,
                         !.disk[c][v].ab = w.ballot,
                         !.disk[c][v].base = JournalNone,
                         !.disk[c][v].tail = w.seq,
                         !.pending[c][v] = JournalNone,
                         !.callbacks = @ \cup {w},
                         !.acceptedHistory = hist,
                         !.chosenHistory = @ \cup NewChosen(p, hist)], <<>>)}
       ELSE IF w.kind = "initialize"
       THEN {Transition("journal.successor.persist",
              [s EXCEPT !.disk[c][v] =
                  [EmptyVote EXCEPT !.initialized = TRUE, !.tail = w.seq],
                        !.pending[c][v] = JournalNone], <<>>)}
       ELSE {}

UnlearnedVotes(s, c, v, votes) ==
    {r \in votes : Len(r.seq) > Len(s.learned[c][v].seq)}

PromiseSent(s, c, b, v) ==
    \E m \in s.net : m.kind = "promise" /\ m.src = v /\
       m.body.cfg = c /\ m.body.ballot = b

Callback(p, s, w) ==
    LET c == w.cfg
        b == w.ballot
        v == w.voter
        leader == Leader(p, c, b)
        own == Vote(c, b, v, w.seq)
    IN IF w.inc # s.inc[v] \/ v \in s.destroyed
       THEN {Transition("journal.callback.stale",
              [s EXCEPT !.callbacks = @ \ {w}], <<>>)}
       ELSE IF w.kind = "promise" /\ PromiseSent(s, c, b, v)
       THEN {Transition("journal.promise.duplicate",
              [s EXCEPT !.callbacks = @ \ {w}], <<>>)}
       ELSE IF w.kind = "promise"
       THEN {Transition("journal.promise.reply",
              [s EXCEPT !.callbacks = @ \ {w},
                !.net = @ \cup {Message("promise", c, b, v, leader,
                                          [ab |-> w.cert, seq |-> w.seq])}], <<>>)}
       ELSE IF w.kind = "accept"
       THEN LET outgoing ==
              IF v = leader
              THEN {Message("accept", c, b, v, target,
                          [seq |-> w.seq, evidence |-> own]) :
                                      target \in p.members[c] \ {v}}
              ELSE {Message("accepted", c, b, v, leader, own)}
                known == IF w.cert = JournalNone THEN {own} ELSE {own, w.cert}
            IN {Transition("journal.accept.reply",
                 [s EXCEPT !.callbacks = @ \ {w},
                           !.net = @ \cup outgoing,
                           !.knowledge[c][v] = UnlearnedVotes(s, c, v, @ \cup known)], <<>>)}
       ELSE {}

BeginWrite(p, s, c, v, w, tag) ==
    LET submitted == [s EXCEPT !.pending[c][v] = w]
        abstract == IF "abstractVoters" \in DOMAIN p THEN v \in p.abstractVoters ELSE FALSE
    IN IF ~abstract THEN {Transition(tag, submitted, <<>>)}
       ELSE UNION {LET fresh == t.next.callbacks \ s.callbacks
                   IN IF fresh = {} THEN {t}
                      ELSE UNION {{Transition(tag, cb.next, cb.emissions) :
                          cb \in Callback(p, t.next, done)} : done \in fresh}
                   : t \in CompleteWrite(p, submitted, c, v)}

StartBallot(p, s, c, b) ==
    LET leader == Leader(p, c, b)
    IN IF s.leaders[c][b].phase = "idle" /\ Usable(p, s, c, leader)
       THEN {Transition("journal.prepare",
            [s EXCEPT !.leaders[c][b].phase = "prepare",
              !.net = @ \cup {Message("prepare", c, b, leader, v, JournalNone) :
                                v \in p.members[c]}], <<>>)}
       ELSE {}

Write(kind, c, b, v, inc, seq, cert) ==
    [kind |-> kind, cfg |-> c, ballot |-> b, voter |-> v,
     inc |-> inc, seq |-> seq, cert |-> cert]

PrepareReceive(p, s, m) ==
    LET c == m.body.cfg
        b == m.body.ballot
        v == m.dst
        d == s.disk[c][v]
        reply == Message("promise", c, b, v, Leader(p, c, b),
                         [ab |-> d.ab, seq |-> Full(s, c, v)])
    IN IF Usable(p, s, c, v) /\ b >= d.promise /\
          (s.pending[c][v] = JournalNone \/ p.mode = "unsynchronized_prepare")
       THEN IF b = d.promise /\ PromiseSent(s, c, b, v) THEN {}
            ELSE IF b = d.promise
            THEN {Transition("journal.promise.retry",
                    [s EXCEPT !.net = @ \cup {reply},
                       !.hints[c][v] = b], <<>>)}
            ELSE IF p.mode = "volatile_promise"
            THEN {Transition("journal.promise.volatile",
                   [s EXCEPT !.net = @ \cup {reply},
                      !.hints[c][v] = b], <<>>)}
            ELSE IF p.mode = "unsynchronized_prepare" /\ s.pending[c][v] # JournalNone
            THEN {Transition("journal.promise.overtake",
                   [s EXCEPT !.disk[c][v].promise = b,
                     !.net = @ \cup {reply}, !.hints[c][v] = b], <<>>)}
            ELSE BeginWrite(p, [s EXCEPT !.hints[c][v] = b], c, v,
                   Write("promise", c, b, v, s.inc[v], <<>>, JournalNone),
                   "journal.promise.submit")
       ELSE {}

PromiseReceive(p, s, m) ==
    LET c == m.body.cfg
        b == m.body.ballot
        r == [voter |-> m.src, ab |-> m.body.value.ab,
              seq |-> m.body.value.seq]
    IN IF s.leaders[c][b].phase = "prepare" /\ r \notin s.leaders[c][b].replies
       THEN {Transition("journal.promise.receive",
          [s EXCEPT !.leaders[c][b].replies =
             {old \in @ : old.voter # r.voter} \cup {r}], <<>>)}
       ELSE {}

PrepareReady(p, s, c, b) ==
    LET rs == s.leaders[c][b].replies
        ready == Cardinality({r.voter : r \in rs}) * 2
                     > Cardinality(p.members[c])
    IN IF s.leaders[c][b].phase = "prepare" /\ ready
       THEN LET best == IF p.mode = "wrong_recovery"
                         THEN CHOOSE r \in rs :
                           \A other \in rs :
                             r.ab < other.ab \/
                             (r.ab = other.ab /\ Len(r.seq) <= Len(other.seq))
                         ELSE CHOOSE r \in rs :
                           \A other \in rs :
                             r.ab > other.ab \/
                             (r.ab = other.ab /\ Len(r.seq) >= Len(other.seq))
            IN {Transition("journal.prepared",
              [s EXCEPT !.leaders[c][b].phase = "ready",
                        !.leaders[c][b].seq = best.seq,
                        !.proposalHistory = @ \cup {[cfg |-> c, ballot |-> b, seq |-> best.seq]},
                        !.leaders[c][b].replies = {}], <<>>)}
       ELSE {}

Forward(p, s, c, v, command) ==
    LET cert == s.learned[c][v]
        b == s.hints[c][v]
    IN IF v \in s.destroyed THEN {}
       ELSE IF Terminal(c, cert.seq)
       THEN LET target == cert.seq[Len(cert.seq)].body.target
                message == Message("submit", target, 1, v, Leader(p, target, 1), command)
            IN IF message \notin s.net
               THEN {Transition("journal.forward.successor",
                      [s EXCEPT !.net = @ \cup {message}], <<>>)} ELSE {}
       ELSE IF b \in p.ballots /\ v # Leader(p, c, b)
            THEN LET message == Message("submit", c, b, v, Leader(p, c, b), command)
                 IN IF message \notin s.net
                    THEN {Transition("journal.forward",
                      [s EXCEPT !.net = @ \cup {message}], <<>>)} ELSE {}
            ELSE {}

SubmitReceive(p, s, m) ==
    LET c == m.body.cfg
        v == m.dst
    IN IF v \notin s.destroyed /\ m.body.value \notin s.requests[c][v]
       THEN {Transition("journal.submission.receive",
              [s EXCEPT !.requests[c][v] = @ \cup {m.body.value}], <<>>)}
       ELSE {}

AppendCommand(p, s, c, b, command) ==
    LET seq == s.leaders[c][b].seq
    IN IF s.leaders[c][b].phase = "ready" /\ ~HasCommand(seq, command) /\
          (~Terminal(c, seq) \/ p.mode = "extend_terminal")
       THEN {Transition("journal.append",
                [s EXCEPT !.leaders[c][b].seq = Append(@, command),
                          !.proposalHistory = @ \cup {[cfg |-> c, ballot |-> b, seq |-> Append(seq, command)]},
                          !.proposed = @ \cup {command}], <<>>)}
       ELSE {}

AppendHandoff(p, s, c, b, target) ==
    LET seq == s.leaders[c][b].seq
        command == Command(p.owner[c], <<"handoff", c, target, seq>>,
                      "journal.handoff",
                      [source |-> c, target |-> target, prefix |-> seq])
    IN IF s.leaders[c][b].phase = "ready" /\ Len(seq) > 0 /\
          ~Terminal(c, seq) /\ target \in p.successors[c]
       THEN {Transition("journal.append.handoff",
                [s EXCEPT !.leaders[c][b].seq = Append(@, command),
                          !.proposalHistory = @ \cup {[cfg |-> c, ballot |-> b, seq |-> Append(seq, command)]},
                          !.proposed = @ \cup {command}], <<>>)}
       ELSE {}

SelfAccept(p, s, c, b) ==
    LET v == Leader(p, c, b)
        seq == s.leaders[c][b].seq
        d == s.disk[c][v]
    IN IF s.leaders[c][b].phase = "ready" /\ Len(seq) > 0 /\
          Usable(p, s, c, v) /\ s.pending[c][v] = JournalNone /\
          b >= d.promise /\ (b # d.ab \/ seq # Full(s, c, v))
       THEN BeginWrite(p, s, c, v,
              Write("accept", c, b, v, s.inc[v], seq, JournalNone),
              "journal.accept.self.submit")
       ELSE {}

AcceptReceive(p, s, m) ==
    LET c == m.body.cfg
        b == m.body.ballot
        v == m.dst
        seq == m.body.value.seq
        evidence == m.body.value.evidence
        d == s.disk[c][v]
        ownVote == Vote(c, b, v, seq)
        reply == Message("accepted", c, b, v, Leader(p, c, b), ownVote)
        known == {evidence, ownVote}
    IN IF Usable(p, s, c, v) /\ s.pending[c][v] = JournalNone /\ b >= d.promise /\
          evidence.voter = Leader(p, c, b) /\ evidence.ballot = b /\
          evidence.cfg = c /\ evidence.seq = seq
       THEN IF d.ab = b /\ Prefix(seq, Full(s, c, v))
            THEN IF reply \notin s.net \/ UnlearnedVotes(s, c, v, known) \ s.knowledge[c][v] # {}
                 THEN {Transition("journal.accept.retry",
                    [s EXCEPT !.net = @ \cup {reply},
                       !.knowledge[c][v] = UnlearnedVotes(s, c, v, @ \cup known)], <<>>)}
                 ELSE {}
            ELSE IF d.ab # b \/ Prefix(Full(s, c, v), seq)
            THEN BeginWrite(p,
                  [s EXCEPT !.knowledge[c][v] =
                     IF p.mode = "ack_before_persist" THEN @ \cup known ELSE @],
                  c, v, Write("accept", c, b, v, s.inc[v], seq, evidence),
                  "journal.accept.submit")
            ELSE {}
       ELSE {}


\* At nonfault actors the local persistence callback is hidden: its messages
\* remain independently deliverable. Submission and physical completion remain
\* separate transitions. JournalPersistence checks the durable-state relation.
FinishWrite(p, s, c, v) ==
    LET completed == CompleteWrite(p, s, c, v)
        fold == IF "atomicVoters" \in DOMAIN p THEN v \in p.atomicVoters ELSE FALSE
    IN IF ~fold THEN completed
       ELSE UNION {LET fresh == t.next.callbacks \ s.callbacks
                   IN IF fresh = {} THEN {t}
                      ELSE UNION {{Transition(t.tag, cb.next, cb.emissions) :
                          cb \in Callback(p, t.next, w)} : w \in fresh}
                   : t \in completed}

AcceptedReceive(p, s, m) ==
    LET c == m.body.cfg
        v == m.dst
    IN IF v \notin s.destroyed /\
          UnlearnedVotes(s, c, v, {m.body.value}) \ s.knowledge[c][v] # {}
       THEN {Transition("journal.evidence.receive",
               [s EXCEPT !.knowledge[c][v] = UnlearnedVotes(s, c, v, @ \cup {m.body.value})], <<>>)}
       ELSE {}

Learn(p, s, c, v, r, seq) ==
    LET votes == MatchingVotes(p, s.knowledge[c][v], c, r.ballot, seq)
        opaque == IF "opaqueCertificates" \in DOMAIN p THEN p.opaqueCertificates ELSE FALSE
        cert == [cfg |-> c, ballot |-> r.ballot, seq |-> seq,
                 votes |-> IF opaque THEN JournalNone ELSE votes]
    IN IF Len(seq) > Len(s.learned[c][v].seq) /\
          Quorum(p, votes, c, r.ballot, seq) /\
          v \notin s.destroyed
       THEN {Transition("journal.learn",
               [s EXCEPT !.learned[c][v] = cert,
                         !.knowledge[c][v] =
                            {old \in @ : Len(old.seq) > Len(seq)}], <<>>)}
       ELSE {}

Transfer(p, s, c, v) ==
    LET cert == s.learned[c][v]
    IN IF Terminal(c, cert.seq)
       THEN LET targets == IF p.mode = "wrong_successor"
                            THEN p.successors[c]
                            ELSE {cert.seq[Len(cert.seq)].body.target}
            IN {Transition("journal.transfer",
                 [s EXCEPT !.net = @ \cup UNION
                    {{Message("initialize", target, 1, v, dest, cert) :
                              dest \in p.members[target]} : target \in targets}], <<>>)}
       ELSE {}

InitializeReceive(p, s, m) ==
    LET c == m.body.cfg
        v == m.dst
        cert == m.body.value
        command == cert.seq[Len(cert.seq)]
    IN IF v \notin s.destroyed /\ ~s.disk[c][v].initialized /\
          s.pending[c][v] = JournalNone /\ ValidCert(p, cert) /\
          command.kind = "journal.handoff" /\
          (command.body.target = c \/ p.mode = "wrong_successor") /\
          Prefix(command.body.prefix, cert.seq)
       THEN BeginWrite(p, s, c, v,
                  Write("initialize", c, 1, v, s.inc[v], cert.seq, cert),
                  "journal.successor.submit")
       ELSE {}

Deliver(p, s, c, v, actor) ==
    LET j == p.owner[c]
        i == s.delivered[actor][j] + 1
        cert == s.learned[c][v]
        event == Event(<<j, i, actor>>, v, actor, "journal.deliver",
                   [owner |-> j, index |-> i, command |-> cert.seq[i], config |-> c])
    IN IF actor \in p.subscribers[j] /\ i <= Len(cert.seq) /\
          v \notin s.destroyed /\
          ~(\E r \in s.recoveries : r.owner = j /\ r.actor = actor)
       THEN {Transition("journal.deliver",
               [s EXCEPT !.delivered[actor][j] = i,
                         !.outputHistory = @ \cup {event}], <<event>>)}
       ELSE {}

RecoverSnapshot(p, s, c, v, r, i) ==
    LET cert == s.learned[c][v]
        command == cert.seq[i]
        prefix == SubSeq(cert.seq, 1, i)
        event == Event(<<"snapshot", r.owner, r.actor, r.request>>, v, r.actor,
                    "journal.snapshot",
                    [owner |-> r.owner, config |-> c, index |-> i,
                     prefix |-> prefix, recovery |-> r.request])
    IN IF p.owner[c] = r.owner /\ v \notin s.destroyed /\
          command.kind = "journal.barrier" /\
          command.body.actor = r.actor /\ command.body.recovery = r.request
       THEN {Transition("journal.snapshot",
                [s EXCEPT !.recoveries = @ \ {r},
                          !.delivered[r.actor][r.owner] = i,
                          !.outputHistory = @ \cup {event}], <<event>>)}
       ELSE {}

Reset(p, s, v) ==
    {Transition("journal.reset",
       [s EXCEPT !.inc[v] = @ + 1,
         !.knowledge = [c \in p.configs |->
             [n \in p.members[c] |->
                IF n = v THEN {} ELSE s.knowledge[c][n]]],
         !.learned = [c \in p.configs |->
             [n \in p.members[c] |->
                IF n = v THEN EmptyCert(c) ELSE s.learned[c][n]]],
         !.leaders = [c \in p.configs |->
             [b \in p.ballots |->
                IF Leader(p, c, b) = v
                THEN [EmptyLeader EXCEPT !.phase = "dead"]
                ELSE s.leaders[c][b]]],
         !.resetHistory = @ \cup {v}], <<>>)}

Destroy(p, s, v) ==
    {Transition("journal.destroy",
       [s EXCEPT !.destroyed = @ \cup {v},
         !.disk = [c \in p.configs |->
             [n \in p.members[c] |->
                IF n = v THEN EmptyVote ELSE s.disk[c][n]]],
         !.pending = [c \in p.configs |->
             [n \in p.members[c] |->
                IF n = v THEN JournalNone ELSE s.pending[c][n]]]], <<>>)}

ReviveErased(p, s, v) ==
    IF p.mode = "reuse_erased" /\ v \in s.destroyed
    THEN {Transition("journal.erased.reuse",
          [s EXCEPT !.destroyed = @ \ {v},
             !.disk = [c \in p.configs |->
               [n \in p.members[c] |->
                  IF n = v THEN [EmptyVote EXCEPT !.initialized = TRUE]
                  ELSE s.disk[c][n]]]], <<>>)}
    ELSE {}

\* A checkpoint is an immutable, material-bearing encoding of an exact chosen
\* prefix. Accepted but not-yet-learned suffixes remain in the voter's live tail.
Checkpoint(p, s, c, v) ==
    LET cert == s.learned[c][v]
        id == <<c, v, cert.ballot, Len(cert.seq)>>
        blob == [id |-> id, seq |-> cert.seq, cert |-> cert]
    IN IF Len(cert.seq) > 0 /\ Usable(p, s, c, v) /\
          Prefix(cert.seq, Full(s, c, v))
       THEN {Transition("journal.checkpoint.persist",
               [s EXCEPT !.blobs = @ \cup {blob}], <<>>)}
       ELSE {}

Compact(p, s, c, v, blob) ==
    LET seq == Full(s, c, v)
        cut == Len(blob.seq)
    IN IF Usable(p, s, c, v) /\ s.pending[c][v] = JournalNone /\
          blob.id[1] = c /\ blob.id[2] = v /\ Prefix(blob.seq, seq) /\
          (s.disk[c][v].base = JournalNone \/ cut > Len(BaseBytes(s, s.disk[c][v])))
       THEN {Transition("journal.compact.publish",
             [s EXCEPT !.disk[c][v].base = blob.id,
               !.disk[c][v].tail =
                   IF p.mode = "discard_suffix" THEN <<>> ELSE SuffixAfter(seq, cut),
               !.compactHistory = @ \cup {<<c, v, cut>>}], <<>>)}
       ELSE {}

ProcessMessage(p, s, m) ==
    CASE m.kind = "prepare" -> PrepareReceive(p, s, m)
      [] m.kind = "promise" -> PromiseReceive(p, s, m)
      [] m.kind = "accept" -> AcceptReceive(p, s, m)
      [] m.kind = "accepted" -> AcceptedReceive(p, s, m)
      [] m.kind = "submit" -> SubmitReceive(p, s, m)
      [] m.kind = "initialize" -> InitializeReceive(p, s, m)
      [] OTHER -> {}

Actions(p, s) ==
    UNION {StartBallot(p, s, c, b) \cup PrepareReady(p, s, c, b)
            \cup SelfAccept(p, s, c, b)
            \cup UNION {AppendCommand(p, s, c, b, x) :
                         x \in s.requests[c][Leader(p, c, b)]}
            \cup UNION {AppendHandoff(p, s, c, b, target) :
                         target \in s.handoffs[c]} :
                     c \in p.configs, b \in p.ballots}
    \cup UNION {ProcessMessage(p, s, m) : m \in s.net}
    \cup UNION {Callback(p, s, w) : w \in s.callbacks}
    \cup UNION {ReviveErased(p, s, v) : v \in s.destroyed}
    \cup UNION {UNION {
          FinishWrite(p, s, c, v) \cup Transfer(p, s, c, v)
          \cup UNION {Forward(p, s, c, v, x) : x \in s.requests[c][v]}
          \cup UNION {UNION {Learn(p, s, c, v, r, seq) :
                        seq \in Prefixes(r.seq)} : r \in s.knowledge[c][v]}
          \cup UNION {Deliver(p, s, c, v, a) : a \in p.actors}
          \cup UNION {RecoverSnapshot(p, s, c, v, r, i) :
                        r \in s.recoveries, i \in 1..Len(s.learned[c][v].seq)}
          \cup (IF p.compaction
                THEN Checkpoint(p, s, c, v)
                     \cup UNION {Compact(p, s, c, v, x) : x \in s.blobs}
                ELSE {}) :
                v \in p.members[c]} : c \in p.configs}

=============================================================================
