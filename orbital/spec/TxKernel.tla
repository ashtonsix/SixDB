------------------------------ MODULE TxKernel ------------------------------
EXTENDS Contracts, Integers
CONSTANT TxNone
Program == INSTANCE TxProgram

(* Shared logical state machine. Commands enter only through JournalRecord;
   receipt of a submission is not admission. Every driver guard uses its own
   delivered facts. The state is a product of local role records for convenient
   composition, not a permission for one role to inspect another's state. *)
MaxN(xs) == CHOOSE x \in xs : \A y \in xs : x >= y
Stamp(p, t, n) == (Cardinality(p.transactions) + 1) *
                  ((n \div (Cardinality(p.transactions) + 1)) + 1) + t
At(q, t) == IF t \in Elements(q) THEN CHOOSE i \in 1..Len(q) : q[i] = t ELSE 0
Remove(q, t) == SelectSeq(q, LAMBDA u : u # t)
LocalWrites(p, t, shard) == {k \in p.writes[t] : p.home[k] = shard}
Conflict(p, t, u, shard) == LocalWrites(p,t,shard) \cap LocalWrites(p,u,shard) # {}
Invalidates(p, t, key) == p.writes[t] \cap p.dependencies[key] # {}
Driver(t) == <<"driver", t>>
Fold(shard) == <<"fold", shard>>
CID(t, kind, key) == <<t, kind, key>>
Fact(t, kind, key, value, src) ==
  Event(<<t,kind,key>>,src,Driver(t),"tx.fact",
    [tx |-> t, kind |-> kind, key |-> key, value |-> value])
Root(t,k) == <<t,k>>
SourceEvent(p,t,a,phase,cut,scopes,decision,effects) ==
  Event(<<"source",t,a,phase>>,Fold(a),a,"root.source",
     [tx |-> t,cut |-> cut,scopes |-> scopes,phase |-> phase,
      outcomeRef |-> CID(t,"outcome",0),decision |-> decision,effects |-> effects])
ExecutionProfile(p,t) == p.profile @@ [verification |->
  IF t \in p.checked THEN "native-checked" ELSE IF p.approved \/ p.asyncCheck THEN "native-approved" ELSE "wasm"]
RetainEvent(p,t,k,cut,context) ==
  Event(<<"retain",t,k>>,Fold(p.home[k]),p.home[k],"root.retain-cut",
    [context |-> context, tx |-> t,key |-> k,cut |-> cut,
     scopes |-> p.dependencies[k],root |-> Root(t,k),generation |-> 0,
     owner |-> p.home[k],requester |-> Fold(p.home[k])])
FactKey(kind,key) == ToString(<<kind,key>>)
Knows(s,t,kind,key) == FactKey(kind,key) \in DOMAIN s.known[t]
Learned(s,t,kind,key) ==
  IF Knows(s,t,kind,key) THEN s.known[t][FactKey(kind,key)] ELSE TxNone
HasAll(s,t,kind,keys) == \A k \in keys : Knows(s,t,kind,k)

Init(p) ==
  [begun |-> {}, profiles |-> [t \in p.transactions |-> TxNone], enrolled |-> [t \in p.transactions |-> {}],
   queue |-> [a \in p.shards |-> <<>>], foldUp |-> [a \in p.shards |-> TRUE],
   ticket |-> [t \in p.transactions |-> [a \in p.shards |-> "none"]],
   minimum |-> [t \in p.transactions |-> [a \in p.shards |-> 0]],
   position |-> [t \in p.transactions |-> 0],
   fixed |-> [t \in p.transactions |-> [a \in p.shards |-> 0]],
   bounds |-> [k \in p.keys |-> 0],
   registered |-> [t \in p.transactions |-> {}],
   readCut |-> [t \in p.transactions |-> [k \in p.keys |-> 0]],
   readResult |-> [t \in p.transactions |-> [k \in p.keys |-> TxNone]],
   material |-> [t \in p.transactions |-> [k \in p.keys |-> TxNone]],
   failedReads |-> [t \in p.transactions |-> {}],
   executionErrors |-> [t \in p.transactions |-> TxNone],
   executionEvidence |-> [t \in p.transactions |-> TxNone],
   known |-> [t \in p.transactions |-> [k \in {} |-> TxNone]],
   inputs |-> [t \in p.transactions |-> [k \in p.keys |-> TxNone]],
   private |-> [t \in p.transactions |-> TxNone],
   reports |-> [t \in p.transactions |-> [r \in {1,2} |-> TxNone]],
   outcome |-> [t \in p.transactions |-> TxNone],
   decision |-> [t \in p.transactions |-> TxNone],
   cancelled |-> [t \in p.transactions |-> FALSE],
   localCancel |-> [a \in p.shards |-> {}],
   resolved |-> [a \in p.shards |-> {}], versions |-> {},
   recorded |-> {}, replies |-> [id \in {} |-> <<>>], commands |-> [id \in {} |-> TxNone], commandClash |-> FALSE, journalIndex |-> [a \in p.shards |-> 0], sent |-> {}, observations |-> {},
   published |-> {}, up |-> [t \in p.transactions |-> TRUE],
   incarnation |-> [t \in p.transactions |-> 0], crashes |-> 0,
   sourceReady |-> (p.slow = {}), retiredSources |-> {}, otherWork |-> FALSE,
   grantOrder |-> [a \in p.shards |-> <<>>],
   requestOrder |-> [a \in p.shards |-> <<>>],
   evidence |-> {}, viewRequests |-> {}, protected |-> {},
   mapOpen |-> TRUE, migrating |-> FALSE, migrated |-> FALSE, incidents |-> {}, asyncReports |-> [t \in p.transactions |-> TxNone], oldMapPlans |-> {}]

Request(p,s,t,kind,key,owner,body) ==
  LET id == CID(t,kind,key)
      cmd == Command(owner,id,kind,[tx |-> t, key |-> key, data |-> body])
  IN Transition("tx.submit." \o kind,
       [s EXCEPT !.sent = @ \cup {id}],
       <<Event(<<id,s.incarnation[t]>>,Driver(t),owner,"journal.submit",cmd)>>)

(* Capture only the source owner's state. This scoped payload supplements the
   recovered journal prefix; later locally folded records remain authoritative.
   Completed old-map plans have no live reservation or read continuation to move. *)
ScopedTransfer(p,s,a) ==
  LET keys=={k \in p.keys:p.home[k]=a}
      ids=={id \in s.recorded:s.commands[id].owner=a}
  IN [bounds |-> [k \in keys |-> s.bounds[k]],
      versions |-> {v \in s.versions:v.key \in keys},recorded |-> ids,
      commands |-> [id \in ids |-> s.commands[id]],
      replies |-> [id \in ids |-> s.replies[id]]]

TransferRequest(p,s,a) ==
  LET tr==Request(p,s,1,"map-transfer",0,a,ScopedTransfer(p,s,a))
  IN [tr EXCEPT !.emissions=[i \in 1..Len(@) |-> [@ [i] EXCEPT !.src=Fold(a)]]]

LocalFloor(p,s,t,a) ==
  MaxN({p.causal[t]} \cup {s.bounds[k] : k \in LocalWrites(p,t,a)} \cup
       {IF s.fixed[u][a]>0 THEN s.fixed[u][a] ELSE s.minimum[u][a] :
          u \in {v \in p.transactions \ {t} : Conflict(p,t,v,a)}})
Holds(s,t,a) == s.ticket[t][a] \in {"held","announced"}
CanGrant(p,s,t,a) ==
  /\ s.foldUp[a]
  /\ t \in Elements(s.queue[a])
  /\ \A u \in p.transactions \ {t} : Holds(s,u,a) => ~Conflict(p,t,u,a)
  /\ p.bug="overtake" \/
       \A i \in 1..(At(s.queue[a],t)-1) : ~Conflict(p,t,s.queue[a][i],a)

(* Granting is a deterministic closure of a delivered durable input. A local
   enabled queue transition is independent of application execution. *)
Grant(p,s,t,a) ==
  Transition("tx.grant",
    [s EXCEPT !.queue[a]=Remove(@,t), !.ticket[t][a]="held",
              !.grantOrder[a]=Append(@,t)],
    <<Fact(t,"grant",a,TRUE,Fold(a))>>)

WriteEffect(p,s,t) ==
  CASE p.program[t]="put" -> [k \in p.writes[t] |-> p.value[t]]
    [] p.program[t]="none" -> [k \in {} |-> 0]
    [] p.program[t]="read" -> [k \in {} |-> 0]
    [] p.program[t]="increment" -> [k \in p.writes[t] |-> s.inputs[t][p.firstRead[t]]+1]
    [] OTHER -> [k \in p.writes[t] |-> s.inputs[t][p.firstRead[t]]+s.inputs[t][p.secondRead[t]]]
ReturnValue(p,s,t) ==
  CASE p.program[t]="read" -> s.inputs[t][p.firstRead[t]]
    [] p.program[t]="increment" -> s.inputs[t][p.firstRead[t]]+1
    [] p.program[t]="sum" -> s.inputs[t][p.firstRead[t]]+s.inputs[t][p.secondRead[t]]
    [] OTHER -> p.value[t]
Context(p,s,t) == <<t,s.position[t],s.profiles[t],p.mapVersion>>
\* Historical observation of the actual primary execution, before the owner
\* decorates an outcome as failed. No protocol action reads this observer.
CaptureExecution(p,s,t,o) == [s EXCEPT !.private[t]=o,!.executionEvidence[t]=o]

(* Select newest installed position, never the last arrival. A committed
   complete replacement shadows older possibilities only on this key. *)
(* Re-execution can follow physical installation of this transaction's output.
   Its captured source still excludes that output; only its private program
   supplies read-own-effects. This matches the concrete retained-cut provider. *)
VisibleVersions(p,s,t,k) == {v \in s.versions : v.key=k /\ v.position<=s.readCut[t][k] /\
                                               (v.tx#t \/ p.bug="self-source")}
VisiblePosition(p,s,t,k) == MaxN({0} \cup {v.position : v \in VisibleVersions(p,s,t,k)})
ShadowPosition(p,s,t,k,u) ==
  MaxN({0} \cup {v.position:v \in {w \in VisibleVersions(p,s,t,k):
                    p.writes[u] \cap p.dependencies[k] \subseteq w.coverage}})
ReadBlocked(p,s,t,k) ==
  \E u \in p.transactions \ {t} :
    /\ Invalidates(p,u,k)
    /\ LET a == p.home[k]
           lower == IF s.fixed[u][a]>0 THEN s.fixed[u][a] ELSE s.minimum[u][a]
       IN /\ s.ticket[u][a] \in {"announced","fixed"}
          /\ lower<=s.readCut[t][k]
          /\ (s.fixed[u][a]=0 \/ lower>ShadowPosition(p,s,t,k,u))
ReadValue(p,s,t,k) ==
  IF s.private[t]#TxNone /\ k \in DOMAIN s.private[t].effects
  THEN s.private[t].effects[k]
  ELSE IF VisibleVersions(p,s,t,k)={} THEN 0
  ELSE (CHOOSE v \in VisibleVersions(p,s,t,k) :
          v.position=VisiblePosition(p,s,t,k)).value

(* Durable command application; each branch uses only the target owner's
   logical state plus the immutable command/evidence it has received. *)
Apply(p,s,c) ==
  LET t==c.body.tx k==c.body.key d==c.body.data a==c.owner
  IN CASE c.kind="begin" ->
       [next |-> [s EXCEPT !.begun=@ \cup {t}, !.profiles[t]=
                    IF p.bug="wrong-source-version" THEN [d.profile EXCEPT !.sourceVersion="code-v2"] ELSE d.profile],
        emissions |-> <<Fact(t,"begin",0,TRUE,Fold(a))>>]
  [] c.kind="map-close" ->
       [next |-> [s EXCEPT !.mapOpen=FALSE, !.migrating=TRUE, !.oldMapPlans=s.begun],emissions |-> <<>>]
  [] c.kind="map-transfer" ->
       [next |-> [s EXCEPT !.migrating=FALSE, !.migrated=TRUE,
          !.bounds=[x \in p.keys |-> IF x \in DOMAIN d.bounds
                     THEN IF p.bug="migration-drop-floor" THEN 0 ELSE MaxN({@ [x],d.bounds[x]})
                     ELSE @ [x]],
          !.versions=@ \cup d.versions, !.recorded=@ \cup d.recorded,
          !.commands=@ @@ d.commands, !.replies=@ @@ d.replies,
          !.commandClash=@ \/ (\E id \in DOMAIN s.commands \cap DOMAIN d.commands:s.commands[id]#d.commands[id])],
        emissions |-> <<>>]
  [] c.kind="async-report" ->
       [next |-> [s EXCEPT !.asyncReports[t]=d,
         !.incidents=IF d.context#s.outcome[t].context \/ d.result#s.outcome[t].result THEN @ \cup {t} ELSE @],emissions |-> <<>>]
  [] c.kind="enroll" ->
       IF (s.mapOpen \/ t \in s.oldMapPlans \/ p.bug="late-enroll") /\ t \notin s.localCancel[a]
       THEN [next |-> [s EXCEPT !.enrolled[t]=@ \cup {a}],
             emissions |-> <<Fact(t,"enroll",a,TRUE,Fold(a))>>]
       ELSE [next |-> s, emissions |-> <<Fact(t,"enroll",a,FALSE,Fold(a))>>]
  [] c.kind="reserve" ->
       IF t \in s.localCancel[a] /\ p.bug#"resurrect"
       THEN [next |-> s, emissions |-> <<Fact(t,"cancelled",a,TRUE,Fold(a))>>]
       ELSE [next |-> [s EXCEPT !.queue[a]=Append(@,t), !.ticket[t][a]="queued",
                               !.requestOrder[a]=Append(@,t)], emissions |-> <<>>]
  [] c.kind="announce" ->
       IF t \in s.localCancel[a] /\ p.bug#"resurrect"
       THEN [next |-> s,emissions |-> <<Fact(t,"cancelled",a,TRUE,Fold(a))>>]
       ELSE LET m==Stamp(p,t,LocalFloor(p,s,t,a))
       IN [next |-> [s EXCEPT !.minimum[t][a]=m, !.ticket[t][a]="announced"],
           emissions |-> <<Fact(t,"minimum",a,m,Fold(a))>> \o
             (IF p.material THEN <<SourceEvent(p,t,a,"announce",m,LocalWrites(p,t,a),TxNone,[x \in {} |-> 0])>> ELSE <<>>)]
  [] c.kind="snapshot" ->
       LET m==Stamp(p,t,MaxN({p.causal[t]} \cup {s.bounds[x]:x \in {y \in p.keys:p.home[y]=a}} \cup
                                   {s.fixed[u][a]:u \in p.transactions}))
       IN [next |-> s, emissions |-> <<Fact(t,"minimum",a,m,Fold(a))>>]
  [] c.kind="position" ->
       IF s.cancelled[t]
       THEN [next |-> s, emissions |-> <<Fact(t,"cancel",0,TRUE,Fold(a))>>]
       ELSE [next |-> [s EXCEPT !.position[t]=d],
             emissions |-> <<Fact(t,"position",0,d,Fold(a))>>]
  [] c.kind="fix" ->
       [next |-> [s EXCEPT !.fixed[t][a]=d, !.ticket[t][a]="fixed"],
        emissions |-> <<Fact(t,"fix",a,d,Fold(a))>> \o
          (IF p.material THEN <<SourceEvent(p,t,a,"fix",d,LocalWrites(p,t,a),TxNone,[x \in {} |-> 0])>> ELSE <<>>)]
  [] c.kind="bound" ->
       [next |-> [s EXCEPT !.bounds=[x \in p.keys |->
                   IF x \in p.dependencies[k] THEN MaxN({@ [x],d.cut}) ELSE @ [x]],
                          !.registered[t]=@ \cup {k}, !.readCut[t][k]=d.cut],
        emissions |-> <<Fact(t,"bound",k,d.cut,Fold(a))>> \o
          (IF p.material THEN <<RetainEvent(p,t,k,d.cut,d.context)>> ELSE <<>>)]
  [] c.kind="report" ->
       [next |-> [s EXCEPT !.reports[t][k]=d,
                    !.bounds=IF p.bug="checker-bound" THEN [x \in p.keys |-> @ [x]+1] ELSE @],
        emissions |-> <<Fact(t,"report",k,d,Fold(a))>>]
  [] c.kind="outcome" ->
       [next |-> [s EXCEPT !.outcome[t]=d],
        emissions |-> <<Fact(t,"outcome",0,d,Fold(a))>>]
  [] c.kind="decision" ->
       [next |-> [s EXCEPT !.decision[t]=d],
        emissions |-> <<Fact(t,"decision",0,d,Fold(a))>>]
  [] c.kind="install" ->
       LET fresh==IF d.decision="commit"
                  THEN {[tx |-> t,key |-> x,position |-> d.position,value |-> d.outcome.effects[x],coverage |-> {x}] :
                         x \in {y \in DOMAIN d.outcome.effects : p.home[y]=a}}
                  ELSE {}
           retained==IF p.bug="last-arrival"
                     THEN {v \in s.versions : v.key \notin {w.key:w \in fresh}} ELSE s.versions
       IN [next |-> [s EXCEPT !.ticket[t][a]="resolved", !.resolved[a]=@ \cup {t},
                              !.versions=retained \cup fresh],
           emissions |-> <<Fact(t,"installed",a,TRUE,Fold(a))>> \o
             (IF p.material THEN <<SourceEvent(p,t,a,"resolve",d.position,LocalWrites(p,t,a),d.decision,
                  IF d.decision="commit" THEN d.outcome.effects ELSE [x \in {} |-> 0])>> ELSE <<>>)]
  [] c.kind="cancel" ->
       IF s.position[t]=0
       THEN [next |-> [s EXCEPT !.cancelled[t]=TRUE, !.decision[t]="abort"],
             emissions |-> <<Fact(t,"cancel",0,TRUE,Fold(a))>>]
       ELSE [next |-> s, emissions |-> <<Fact(t,"cancel-rejected",0,TRUE,Fold(a))>>]
  [] c.kind="cancel-part" ->
       [next |-> [s EXCEPT !.localCancel[a]=@ \cup {t}, !.queue[a]=Remove(@,t),
                          !.ticket[t][a]="resolved", !.resolved[a]=@ \cup {t}],
        emissions |-> <<Fact(t,"installed",a,TRUE,Fold(a))>>]
  [] OTHER -> [next |-> s, emissions |-> <<>>]

CommitAndFold(p,s,c) ==
  LET old==c.id \in DOMAIN s.replies
  IN IF c.id \in s.recorded
     THEN LET prior==IF ~old THEN <<>> ELSE s.replies[c.id]
              reads==IF c.kind="bound" /\ s.readResult[c.body.tx][c.body.key]#TxNone
                     THEN <<Fact(c.body.tx,"read",c.body.key,s.readResult[c.body.tx][c.body.key],Fold(c.owner))>>
                     ELSE <<>>
          grants==IF c.kind="reserve" /\ s.ticket[c.body.tx][c.owner] \in {"held","announced","fixed","resolved"} /\
                         c.body.tx \notin s.localCancel[c.owner]
                      THEN <<Fact(c.body.tx,"grant",c.owner,TRUE,Fold(c.owner))>> ELSE <<>>
          IN {Transition("tx.replay-command",[s EXCEPT !.commandClash=@ \/ (c.id \in DOMAIN s.commands /\ s.commands[c.id]#c), !.commands=@ @@ (c.id :> c)],prior \o reads \o grants)}
     ELSE LET step==Apply(p,s,c)
          IN {Transition("tx.apply." \o c.kind,
                 [step.next EXCEPT !.recorded=@ \cup {c.id}, !.commandClash=@ \/ (c.id \in DOMAIN s.commands /\ s.commands[c.id]#c), !.commands=@ @@ (c.id :> c),
                    !.replies=@ @@ (c.id :> step.emissions)],step.emissions)}

RECURSIVE DrainGrants(_,_,_)
DrainGrants(p,s,a) ==
  LET enabled=={t \in p.transactions:CanGrant(p,s,t,a)}
  IN IF enabled={} THEN [next |-> s,emissions |-> <<>>]
     ELSE LET t==CHOOSE x \in enabled: \A y \in enabled:x<=y
              step==Grant(p,s,t,a)
              rest==DrainGrants(p,step.next,a)
          IN [next |-> rest.next,emissions |-> step.emissions \o rest.emissions]

(* Erase a replaced logical owner's application cache. The provider's durable
   prefix and other owners remain. Snapshot replay must rebuild every erased
   ordering fact before this owner can serve again. Historical observations are
   ghost audit evidence and do not authorize any action. *)
CrashFold(p,s,a) ==
  LET owns=={t \in p.transactions:p.owner[t]=a}
      localKeys=={k \in p.keys:p.home[k]=a}
      ids=={id \in DOMAIN s.commands:s.commands[id].owner=a}
  IN [s EXCEPT !.foldUp[a]=FALSE, !.queue[a]= <<>>, !.grantOrder[a]= <<>>, !.requestOrder[a]= <<>>,
      !.begun=@ \ owns, !.profiles=[t \in p.transactions |-> IF t \in owns THEN TxNone ELSE @ [t]],
      !.enrolled=[t \in p.transactions |-> @ [t] \ {a}],
      !.ticket=[t \in p.transactions |-> [@ [t] EXCEPT ![a]="none"]],
      !.minimum=[t \in p.transactions |-> [@ [t] EXCEPT ![a]=0]],
      !.fixed=[t \in p.transactions |-> [@ [t] EXCEPT ![a]=0]],
      !.bounds=[k \in p.keys |-> IF k \in localKeys THEN 0 ELSE @ [k]],
      !.registered=[t \in p.transactions |-> @ [t] \ localKeys],
      !.readCut=[t \in p.transactions |-> [k \in p.keys |-> IF k \in localKeys THEN 0 ELSE @ [t][k]]],
      !.readResult=[t \in p.transactions |-> [k \in p.keys |-> IF k \in localKeys THEN TxNone ELSE @ [t][k]]],
      !.material=[t \in p.transactions |-> [k \in p.keys |-> IF k \in localKeys THEN TxNone ELSE @ [t][k]]],
      !.position=[t \in p.transactions |-> IF t \in owns THEN 0 ELSE @ [t]],
      !.outcome=[t \in p.transactions |-> IF t \in owns THEN TxNone ELSE @ [t]],
      !.decision=[t \in p.transactions |-> IF t \in owns THEN TxNone ELSE @ [t]],
      !.reports=[t \in p.transactions |-> IF t \in owns THEN [r \in {1,2}|->TxNone] ELSE @ [t]],
      !.cancelled=[t \in p.transactions |-> IF t \in owns THEN FALSE ELSE @ [t]],
      !.localCancel[a]={}, !.resolved[a]={}, !.versions={v \in @:v.key \notin localKeys},
      !.recorded=@ \ ids, !.replies=[id \in DOMAIN @ \ ids |-> @ [id]], !.journalIndex[a]=0,
      !.mapOpen=IF a=1 THEN TRUE ELSE @, !.migrating=IF a=1 THEN FALSE ELSE @,
      !.migrated=IF a=1 THEN FALSE ELSE @, !.oldMapPlans=IF a=1 THEN {} ELSE @]

RECURSIVE ReplayCommands(_,_,_)
ReplayCommands(p,s,commands) ==
  IF commands = <<>> THEN [next |-> s,emissions |-> <<>>]
  ELSE LET first==IF p.bug="replay-drop-bound" /\ Head(commands).kind="bound"
                  THEN Transition("tx.drop-replayed-bound",s,<<>>)
                  ELSE CHOOSE step \in CommitAndFold(p,s,Head(commands)):TRUE
           grants==DrainGrants(p,first.next,Head(commands).owner)
           rest==ReplayCommands(p,grants.next,Tail(commands))
       IN [next |-> rest.next,emissions |-> first.emissions \o grants.emissions \o rest.emissions]

ExecutionReady(p,s,t) ==
  /\ Knows(s,t,"position",0)
  /\ (p.bug="early-execution" \/ HasAll(s,t,"fix",p.parts[t]))
  /\ \A k \in p.reads[t]:s.inputs[t][k]#TxNone
  /\ (t \notin p.slow \/ s.sourceReady)

Receive(p,s,e) ==
  IF e.kind="journal.deliver"
     THEN IF s.foldUp[e.body.owner] /\ e.body.index<=s.journalIndex[e.body.owner]+1
          THEN {Transition(tr.tag,[tr.next EXCEPT !.journalIndex[e.body.owner]=MaxN({@,e.body.index})],tr.emissions):
                  tr \in CommitAndFold(p,s,e.body.command)} ELSE {}
  ELSE IF e.kind="journal.snapshot"
       THEN LET recovered==ReplayCommands(p,[s EXCEPT !.foldUp[e.body.owner]=TRUE],e.body.prefix)
            IN {Transition("tx.recover-prefix",[recovered.next EXCEPT !.journalIndex[e.body.owner]=MaxN({@,e.body.index})],recovered.emissions)}
  ELSE IF e.kind="tx.fact" /\ e.body.tx \in p.transactions
       THEN LET t==e.body.tx
            IN IF s.up[t]
               THEN {Transition("tx.receive." \o e.body.kind,
                     [s EXCEPT !.known[t]=@ @@ (FactKey(e.body.kind,e.body.key) :> e.body.value),
                       !.inputs[t]=IF e.body.kind="read"
                                   THEN [@ EXCEPT ![e.body.key]=e.body.value] ELSE @],<<>>)} ELSE {}
  ELSE IF e.kind="execution.result"
       THEN LET t==e.body.tx o==e.body.outcome
            IN IF t \in p.externalCompute /\ s.private[t]=TxNone /\ ExecutionReady(p,s,t) /\
                  e.body.context=Context(p,s,t) /\ e.body.cut=s.position[t] /\ o.context=e.body.context
               THEN {Transition("tx.external-compute",[s EXCEPT
                     !.executionEvidence[t]=o,
                     !.executionErrors[t]=IF DOMAIN o.effects \subseteq p.writes[t] THEN @ ELSE o,
                     !.private[t]=
                     IF DOMAIN o.effects \subseteq p.writes[t] THEN o
                     ELSE [o EXCEPT !.effects=[k \in {} |-> 0], !.status="bad-envelope"]],<<>>)} ELSE {}
  ELSE IF e.kind="root.cut-protected"
       THEN {Transition("tx.cut-protected",[s EXCEPT !.protected=@ \cup {e.body.root}],<<>>)}
  ELSE IF e.kind="root.recipe-ready"
       THEN IF e.body.cut=s.readCut[e.body.tx][e.body.key] /\ e.body.root \in s.protected
            THEN IF p.physicalReads
                 THEN {Transition("tx.open-view",[s EXCEPT !.viewRequests=@ \cup {e.body.root}],
                      <<Event(<<"view",e.body.tx,e.body.key>>,Fold(p.home[e.body.key]),"views","tx.open-view",
                         [tx |-> e.body.tx,key |-> e.body.key,cut |-> e.body.cut,root |-> e.body.root,
                          context |-> Context(p,s,e.body.tx),recipe |-> e.body])>>)}
                 ELSE {Transition("tx.recipe-ready",
                      [s EXCEPT !.material[e.body.tx][e.body.key]=e.body.values[e.body.key]],<<>>)} ELSE {}
  ELSE IF e.kind="view.observed"
       THEN IF p.physicalReads /\ e.body.root \in s.protected /\
               e.body.context=Context(p,s,e.body.tx) /\
               e.body.cut=s.readCut[e.body.tx][e.body.key]
            THEN {Transition("tx.physical-read",
                [s EXCEPT !.material[e.body.tx][e.body.key]=e.body.values[e.body.key]],<<>>)} ELSE {}
  ELSE IF e.kind="root.unavailable"
       THEN {Transition("tx.source-unavailable",
                [s EXCEPT !.failedReads[e.body.tx]=@ \cup {e.body.key},
                           !.evidence=@ \cup {e.body}],<<>>)}
  ELSE {}

SubmitActions(p,s,t) ==
  LET owner==p.owner[t]
      mines==IF p.parts[t]={} THEN {p.home[p.firstRead[t]]} ELSE p.parts[t]
      pendingEnrol=={a \in p.parts[t]: ~Knows(s,t,"enroll",a)}
      have=={a \in p.parts[t]:Knows(s,t,"grant",a)}
      nextShard==IF p.parts[t] \ have={} THEN 0
                 ELSE CHOOSE a \in p.parts[t] \ have : \A b \in p.parts[t] \ have:a<=b
      completeFix==HasAll(s,t,"fix",p.parts[t])
      c==Learned(s,t,"position",0)
      readyRead=={k \in p.reads[t]:
                     (k=p.firstRead[t] \/ s.inputs[t][p.firstRead[t]]#TxNone)}
      phase==CASE ~Knows(s,t,"begin",0) ->
           {Request(p,s,t,"begin",0,owner,[profile |-> ExecutionProfile(p,t),map |-> p.mapVersion])}
        [] Knows(s,t,"cancel",0) ->
           {Request(p,s,t,"cancel-part",a,a,TRUE):a \in p.parts[t]}
        [] pendingEnrol#{} -> {Request(p,s,t,"enroll",a,a,p.mapVersion):a \in pendingEnrol}
        [] \E a \in p.parts[t]:Learned(s,t,"enroll",a)=FALSE ->
           {Request(p,s,t,"cancel",0,owner,"enrollment-refused")}
        [] have#p.parts[t] -> {Request(p,s,t,"reserve",nextShard,nextShard,p.writes[t])}
        [] ~HasAll(s,t,"minimum",mines) ->
           {Request(p,s,t,IF p.parts[t]={} THEN "snapshot" ELSE "announce",a,a,TRUE):
              a \in {x \in mines:~Knows(s,t,"minimum",x)}}
        [] ~Knows(s,t,"position",0) ->
           {Request(p,s,t,"position",0,owner,MaxN({p.causal[t]} \cup
                    {Learned(s,t,"minimum",a):a \in mines}))}
        [] ~completeFix -> {Request(p,s,t,"fix",a,a,c):
                              a \in {x \in p.parts[t]:~Knows(s,t,"fix",x)}}
        [] s.private[t]=TxNone /\ ~HasAll(s,t,"bound",p.reads[t]) ->
           {Request(p,s,t,"bound",k,p.home[k],[cut |-> c,context |-> Context(p,s,t)]):k \in
              {x \in (IF t \in p.checked THEN p.reads[t] ELSE readyRead):~Knows(s,t,"bound",x)}}
        [] s.private[t]=TxNone -> {}
        [] t \in p.checked /\ s.private[t].status="ok" /\ ~HasAll(s,t,"report",IF p.bug="missing-checker" THEN {1} ELSE {1,2}) -> {}
        [] ~Knows(s,t,"outcome",0) ->
           LET reports=={Learned(s,t,"report",r):r \in {x \in {1,2}:Knows(s,t,"report",x)}}
               matches==(Cardinality(reports)<=1 \/ p.bug="final-only") /\
                        (p.bug="unverified-main" \/ \A r \in reports:r.outcome=s.private[t])
               result==IF t \in p.checked /\ p.bug="fabricated-mismatch"
                       THEN [s.private[t] EXCEPT !.status="mismatch",!.result=@+1]
                       ELSE IF t \in p.checked /\ (~matches \/ p.bug="spurious-mismatch")
                       THEN [s.private[t] EXCEPT !.status="mismatch"] ELSE s.private[t]
           IN {Request(p,s,t,"outcome",0,owner,result)}
        [] ~Knows(s,t,"decision",0) ->
           {Request(p,s,t,"decision",0,owner,
             IF Learned(s,t,"outcome",0).status="ok" /\ p.bug#"always-abort" THEN "commit" ELSE "abort")}
        [] OTHER -> {Request(p,s,t,"install",a,a,
                    [decision |-> Learned(s,t,"decision",0),outcome |-> Learned(s,t,"outcome",0),position |-> c]):
                      a \in {x \in p.parts[t]:~Knows(s,t,"installed",x)}}
  IN {v \in phase : \E e \in Elements(v.emissions):
       CID(t,e.body.kind,e.body.body.key) \notin s.sent /\
       ~(p.bug="omit-resolution" /\ t=1 /\ e.body.kind="install")}

ReadActions(p,s,t) ==
  {LET v==IF p.material THEN s.material[t][k] ELSE ReadValue(p,s,t,k)
   IN Transition("tx.read",
       [s EXCEPT !.readResult[t][k]=v,
          !.observations=@ \cup {[tx |-> t,key |-> k,position |-> s.readCut[t][k],value |-> v]}],
        <<Fact(t,"read",k,v,Fold(p.home[k]))>>) :
      k \in {x \in p.reads[t] : s.foldUp[p.home[x]] /\ s.readResult[t][x]=TxNone /\ x \in s.registered[t] /\
           (~p.material \/ s.material[t][x]#TxNone) /\
           (p.bug="skip-pending" \/ ~ReadBlocked(p,s,t,x))}}

ComputeActions(p,s,t) ==
  IF s.private[t]=TxNone /\ s.failedReads[t]#{}
  THEN {Transition("tx.fail-unavailable",[s EXCEPT !.private[t]=
         [effects |-> [k \in {} |-> 0],result |-> 0,status |-> "unavailable",context |-> Context(p,s,t)]],<<>>)}
  ELSE IF t \notin p.externalCompute /\ s.private[t]=TxNone /\ ExecutionReady(p,s,t)
  THEN {Transition("tx.compute",CaptureExecution(p,s,t,
         Program!Evaluate(p,t,s.inputs[t],Context(p,s,t))),<<>>)} ELSE {}

ReportActions(p,s,t) ==
  IF ~p.externalChecks /\ t \in p.checked /\ s.private[t]#TxNone
  THEN UNION {{Request(p,s,t,"report",r,p.owner[t],
             [context |-> Context(p,s,t),request |-> request,outcome |-> s.private[t]]):
                   request \in (IF p.mismatch THEN {"a","b"} ELSE {"a"})} :
                   r \in {x \in {1,2}:CID(t,"report",x) \notin s.sent}}
  ELSE {}

PublishActions(p,s,t) ==
  IF t \notin s.published /\ HasAll(s,t,"installed",p.parts[t]) /\
     (Knows(s,t,"decision",0) \/ Knows(s,t,"cancel",0))
  THEN {Transition("tx.publish",[s EXCEPT !.published=@ \cup {t}],
      <<Event(<<"result",t>>,Driver(t),"client","tx.published",
          [tx |-> t,position |-> s.position[t],decision |-> s.decision[t],outcome |-> s.outcome[t]])>>)}
  ELSE {}

CrashActions(p,s) ==
  {Transition("tx.driver-crash",
      [s EXCEPT !.up[t]=FALSE, !.known[t]=[k \in {} |-> TxNone], !.inputs[t]=[k \in p.keys |-> TxNone],
        !.private[t]=TxNone, !.sent={id \in @ : id[1]#t},
        !.crashes=@+1, !.incarnation[t]=@+1],<<>>) :
       t \in {x \in p.transactions : p.crash /\ s.crashes=0 /\ s.position[x]>0 /\ x \notin s.published}}

Actions(p,s) ==
  UNION {SubmitActions(p,s,t) \cup ReadActions(p,s,t) \cup ComputeActions(p,s,t) \cup
         ReportActions(p,s,t) \cup PublishActions(p,s,t):t \in {x \in p.transactions:s.up[x]}}
  \cup UNION {{Grant(p,s,t,a): a \in {x \in p.shards:CanGrant(p,s,t,x)}} : t \in p.transactions}
  \cup CrashActions(p,s)
  \cup {Transition("tx.driver-recover",[s EXCEPT !.up[t]=TRUE],
          [a \in 1..Cardinality(p.shards) |-> Event(<<"recover",t,s.incarnation[t],a>>,
             Fold(a),a,"journal.recover",[driver |-> t])]):
           t \in {x \in p.transactions:~s.up[x]}}
  \cup (IF s.sourceReady THEN {} ELSE
       {Transition("tx.source-arrives",[s EXCEPT !.sourceReady=TRUE],<<>>)} )
  \cup (IF p.cancel /\ ~s.cancelled[1] /\ CID(1,"cancel",0) \notin s.sent
        THEN {Request(p,s,1,"cancel",0,p.owner[1],TRUE)} ELSE {})
  \cup (IF p.migration /\ s.mapOpen /\ CID(1,"map-close",0) \notin s.sent
       THEN {Request(p,s,1,"map-close",0,1,TRUE)} ELSE {})
  \cup (IF s.migrating /\ s.oldMapPlans \subseteq s.published /\ CID(1,"map-transfer",0) \notin s.sent
       THEN {TransferRequest(p,s,1)} ELSE {})
  \cup (IF p.asyncCheck /\ 1 \in s.published /\ 1 \notin s.incidents /\ CID(1,"async-report",0) \notin s.sent
       THEN {Request(p,s,1,"async-report",0,1,[context |-> Context(p,s,1),result |-> s.outcome[1].result+delta]):delta \in {0,1}} ELSE {})
  \cup (IF p.bug="early-release" THEN
       UNION {{Transition("tx.early-release",[s EXCEPT !.ticket[t][a]="fixed"],<<>>):
            a \in {x \in p.parts[t]:s.ticket[t][x]="announced" /\ s.position[t]>0}}:
                 t \in p.transactions} ELSE {})
  \cup (IF p.bug="omit-resolution" THEN
       {Transition("tx.unrelated-service",[s EXCEPT !.otherWork=~@],<<>>)} ELSE {})

=============================================================================
