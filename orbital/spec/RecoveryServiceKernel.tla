-------------------------- MODULE RecoveryServiceKernel -------------------------
EXTENDS Contracts, Integers
C == INSTANCE CapacityKernel
V == INSTANCE ViewsKernel
Resources == {"ordinary-memory","ordinary-worker","buffer","worker","metadata","io","send","decode"}
Caps(bad) == [r \in Resources |-> IF bad="no-reserve" /\ r="buffer" THEN 0 ELSE 1]
Zero == [r \in Resources |-> 0]
Ordinary == [Zero EXCEPT !["ordinary-memory"]=1,!["ordinary-worker"]=1]
Control == [r \in Resources |-> IF r \in {"ordinary-memory","ordinary-worker"} THEN 0 ELSE 1]
Init == [pool |-> [C!PoolInit EXCEPT !.leases=("ordinary" :> C!Lease("ordinary","busy","ordinary",Ordinary))],
 backend |-> V!RegistryInit,active |-> "",serial |-> 0,cancelled |-> FALSE,
 completed |-> {},retired |-> {},captured |-> [id \in {}|->id]]
Begin(bad,s,payload) ==
 LET id==ToString(<<"recovery-service",s.serial+1>>)
     request==[id|->id,generation|->s.backend.generation,target|->"recovery",kind|->"send",payload|->payload]
 IN IF s.active=""
    THEN {Transition("service-start",[s EXCEPT !.pool=a.next,!.backend=b.next,!.active=id,
          !.serial=@+1,!.cancelled=FALSE,!.captured=@ @@ (id :> payload)],<<>>):
          a \in C!Request(Caps(bad),s.pool,C!Lease(id,"recovery","bootstrap-SOS",Control)),
          b \in V!RegistryRegister(s.backend,request)} ELSE {}
Steps(bad,s) ==
 {LET complete==\E e \in Elements(tr.emissions):e.kind="BackendCompleted"
      retire==\E e \in Elements(tr.emissions):e.kind="BackendRetired"
      release==retire \/ (bad="early-retire" /\ complete)
      pools==IF release THEN C!Release(s.pool,s.active) ELSE {Transition("keep",s.pool,<<>>)}
  IN Transition(tr.tag,
       [s EXCEPT !.backend=tr.next,
         !.pool=IF pools={} THEN @ ELSE (CHOOSE p \in pools:TRUE).next,
         !.completed=IF complete THEN @ \cup {s.active} ELSE @,
         !.retired=IF retire THEN @ \cup {s.active} ELSE @,
         !.active=IF retire THEN "" ELSE @],
       IF complete THEN <<Event(<<"service-done",s.active>>,"backend","recovery","RecoveryCompleted",
                            [id|->s.active,payload|->s.captured[s.active]])>> ELSE <<>>) :
 tr \in V!RegistryActions([actor|->"backend",owner|->"recovery"],s.backend)}
Cancel(s) == IF s.active#"" /\ ~s.cancelled
 THEN {Transition("cancel-optional-observer",[s EXCEPT !.cancelled=TRUE],<<>>)} ELSE {}
PhysicalLifetime(s) == V!RegistryDebt(s.backend) \subseteq DOMAIN s.pool.leases
Bounded(bad,s) == \A r \in Resources:C!Usage(s.pool,r)<=Caps(bad)[r]
OrdinarySaturated(s) == C!Usage(s.pool,"ordinary-memory")=1 /\ C!Usage(s.pool,"ordinary-worker")=1
=============================================================================
