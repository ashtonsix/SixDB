------------------------- MODULE CutMaterialKernel -------------------------
EXTENDS Contracts, Integers
M == INSTANCE MaterialCore

\* Same-owner attachment to the ordered transaction-command fold. Observe is
\* called in emission order, in the same logical step as read-bound registration.
\* Page/token installation and returned material remain separately scheduled.
Token(k,c) == ToString(<<"version",k,c>>)
Code == "decoder-v1"
Recipe(k,c) == [id |-> Token(k,c),base |-> Token(k,c),code |-> Code,
                patches |-> <<>>,interpretation |-> "cut-bytes-v1"]
Datum(v) == [kind |-> "base",interpretation |-> "cut-bytes-v1",content |-> <<v>>]
CodeDatum == [kind |-> "code",interpretation |-> "cut-bytes-v1",content |-> "replace-byte-v1"]
Max(xs) == CHOOSE n \in xs: \A m \in xs:m<=n
Init(p) ==
 [sources |-> [a \in p.shards |-> [t \in p.transactions |->
      [phase |-> "absent",cut |-> 0,scopes |-> {},decision |-> "",effects |-> [k \in {} |-> 0]]]],
  versions |-> {[tx |-> 0,key |-> k,cut |-> 0,value |-> p.initial[k],token |-> Token(k,0)]:k \in p.keys},
  data |-> (Code :> CodeDatum) @@ [id \in {Token(k,0):k \in p.keys} |->
        Datum(p.initial[CHOOSE k \in p.keys:Token(k,0)=id])],
  pending |-> {},requests |-> {},protected |-> {},sent |-> {},
  closures |-> [id \in {} |-> id],ready |-> {},failed |-> {},closed |-> {},
  observations |-> {},deleted |-> {},lost |-> {},available |-> p.keys]
Root(b) == ToString(b.root)
Invalidates(source,b) == source.scopes \cap b.scopes # {}
KnownVersions(s,b) == {v \in s.versions:v.tx#b.tx /\ v.key=b.key /\ v.cut<=b.cut}
Latest(s,b) == Max({v.cut:v \in KnownVersions(s,b)} \cup {0})
Fallback(s,b) == {v.token:v \in {w \in KnownVersions(s,b):w.cut=Latest(s,b)}}
Pending(p,s,b) ==
 {ToString(z):z \in {x \in p.shards \X p.transactions:
   x[2]#b.tx /\ s.sources[x[1]][x[2]].phase \in {"announce","fix"} /\
   Invalidates(s.sources[x[1]][x[2]],b) /\ s.sources[x[1]][x[2]].cut<=b.cut}}
\* Pending is spelled with a predicate below to avoid any fixture meaning in
\* the protocol. A complete replacement may shadow an older fixed source.
Waits(p,s,b) == \E a \in p.shards,t \in p.transactions:
 LET x == s.sources[a][t]
 IN t#b.tx /\ Invalidates(x,b) /\ x.phase \in {"announce","fix"} /\ x.cut<=b.cut /\
    (x.phase="announce" \/ x.cut>
       (IF x.scopes \cap b.scopes \subseteq {b.key} THEN Latest(s,b) ELSE 0))
Observe(p,s,e) ==
 IF e.kind="root.source"
 THEN LET b == e.body
          a == e.dst
          n == [s EXCEPT !.sources[a][b.tx]=[phase |-> b.phase,cut |-> b.cut,
                   scopes |-> b.scopes,decision |-> ToString(b.decision),effects |-> b.effects]]
          outputs == IF b.phase="resolve" /\ b.decision="commit"
            THEN {[tx |-> b.tx,key |-> k,cut |-> b.cut,value |-> b.effects[k],token |-> Token(k,b.cut)]:
                   k \in {x \in DOMAIN b.effects:p.home[x]=a}} ELSE {}
      IN [n EXCEPT !.versions=@ \cup outputs,!.pending=@ \cup outputs]
 ELSE IF e.kind="root.retain-cut"
 THEN LET b == e.body
          id == Root(b)
          closure == [fallback |-> Fallback(s,b) \cap DOMAIN s.data,
               pending |-> Pending(p,s,b),
               interpretation |-> Code]
      IN [s EXCEPT !.requests=@ \cup {b},!.protected=@ \cup {id},
                   !.closures=@ @@ (id :> closure)]
 ELSE s

RECURSIVE ObserveAll(_,_,_)
ObserveAll(p,s,es) == IF es= <<>> THEN s ELSE ObserveAll(p,Observe(p,s,Head(es)),Tail(es))
Materialize(p,s,v) ==
 IF v \in s.pending /\ v.token \notin DOMAIN s.data
 THEN {Transition("persist-accepted-result-bytes",
       [s EXCEPT !.pending=@ \ {v},!.data=@ @@ (v.token :> Datum(v.value))],<<>>)} ELSE {}
Protection(p,s,b) ==
 LET id == Root(b)
 IN IF id \in s.protected /\ ToString(<<"protected",id>>) \notin s.sent
 THEN {Transition("return-cut-coverage",[s EXCEPT !.sent=@ \cup {ToString(<<"protected",id>>)}],
   <<Event(<<"protected",id>>,b.owner,b.requester,"root.cut-protected",
        [root |-> b.root,tx |-> b.tx,key |-> b.key,cut |-> b.cut,
         context |-> b.context,generation |-> b.generation,coverage |-> s.closures[id]])>>)} ELSE {}
Ready(p,s,b) ==
 LET id == Root(b)
     c == IF p.bad="latest-substitution" THEN Max({v.cut:v \in {w \in s.versions:w.key=b.key /\ w.tx#b.tx}}) ELSE Latest(s,b)
     recipe == Recipe(b.key,c)
 IN IF id \notin s.ready \cup s.failed /\ ~Waits(p,s,b) /\ M!WellTyped(s.data,recipe)
    THEN LET bytes == M!Reconstruct(s.data,recipe)
             body == [root |-> b.root,tx |-> b.tx,key |-> b.key,cut |-> b.cut,
                 context |-> b.context,recipe |-> recipe.id,values |-> (b.key :> bytes[1])]
         IN {Transition("return-exact-recipe",[s EXCEPT !.ready=@ \cup {id},
                    !.observations=@ \cup {[request |-> b,bytes |-> bytes,version |-> c]}],
             <<Event(<<"ready",id>>,b.owner,b.requester,"root.recipe-ready",body)>>)} ELSE {}
Unavailable(p,s,b) ==
 LET id == Root(b)
     token == Token(b.key,Latest(s,b))
 IN IF id \notin s.ready \cup s.failed /\ ~Waits(p,s,b) /\ token \notin DOMAIN s.data /\
       ~(\E v \in s.pending:v.token=token)
    THEN {Transition("report-exact-material-unavailable",[s EXCEPT !.failed=@ \cup {id}],
       <<Event(<<"unavailable",id>>,b.owner,b.requester,"root.unavailable",
          [root |-> b.root,tx |-> b.tx,key |-> b.key,cut |-> b.cut,reason |-> "collected-cut"])>>)} ELSE {}
ProtectedTokens(p,s) ==
 {Code} \cup UNION {s.closures[id].fallback:id \in s.protected \ s.closed} \cup
 {v.token:v \in {w \in s.versions:
    \E b \in s.requests:Root(b) \notin s.closed /\ w.key \in b.scopes /\ w.cut<=b.cut}}
Heads(p,s) ==
 {v.token:v \in {x \in s.versions:x.cut=Max({w.cut:w \in {y \in s.versions:y.key=x.key}})}}
Collect(p,s,id) ==
 IF p.gc /\ id \in DOMAIN s.data /\ id \notin Heads(p,s) /\
    (id \notin ProtectedTokens(p,s) \/ p.bad="drop-pending-coverage")
 THEN {Transition("collect-unrooted-version",[s EXCEPT !.data=[t \in DOMAIN @ \ {id} |-> @[t]],
             !.deleted=@ \cup {id}],<<>>)} ELSE {}
Actions(p,s) ==
 UNION {Materialize(p,s,v):v \in s.pending} \cup
 UNION {Protection(p,s,b) \cup Ready(p,s,b) \cup Unavailable(p,s,b):b \in s.requests} \cup
 UNION {Collect(p,s,id):id \in DOMAIN s.data}
Exact(p,s) == \A o \in s.observations:
 /\ o.version<=o.request.cut
 /\ o.bytes= <<(CHOOSE v \in s.versions:v.key=o.request.key /\ v.cut=o.version /\ v.tx#o.request.tx).value>>
 /\ o.version=Latest(s,o.request)
CoverageRetained(p,s) == \A b \in s.requests:
 Root(b) \notin s.closed /\ Root(b) \notin s.failed =>
  s.closures[Root(b)].fallback \cup {s.closures[Root(b)].interpretation} \subseteq DOMAIN s.data
=============================================================================
