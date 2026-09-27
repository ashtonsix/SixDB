---------------------------- MODULE SOSUnderPressure ----------------------------
EXTENDS AuthorityBootstrapSOS
CONSTANT PressureBug
VARIABLES staged,service,delivered
B == INSTANCE AuthorityBootstrapSOS WITH run <- staged, unrelated <- unrelated
S == INSTANCE RecoveryServiceKernel
allvars == <<run,unrelated,staged,service,delivered>>
PressureInit == Init /\ staged=run /\ service=S!Init /\ delivered={}
Capture == B!PersistPause \/ B!ExportSOS \/ B!RecoverBarrier \/ B!Reconcile
Start ==
 /\ service.active="" /\ staged=run /\ Capture
 /\ LET payload==[exports|->staged'.exports \ run.exports,snapshot|->staged'.snapshot,
                  paused|->staged'.paused,phase|->staged'.phase]
    IN \E tr \in S!Begin(PressureBug,service,payload):service'=tr.next
 /\ UNCHANGED <<run,delivered>>
Backend == \E tr \in S!Steps(PressureBug,service):
 /\ service'=tr.next
 /\ IF tr.emissions= <<>> THEN UNCHANGED <<run,delivered>>
    ELSE /\ run'=staged /\ delivered'=delivered \cup {Head(tr.emissions).body}
 /\ UNCHANGED <<staged,unrelated>>
CancelObserver == \E tr \in S!Cancel(service):
 /\ service'=tr.next /\ UNCHANGED <<run,staged,unrelated,delivered>>
Normal ==
 /\ service.active=""
 /\ (Acquire \/ Activate \/ Handoff \/ (\E n \in {1,2}:Admit(n)) \/ StartSOS \/
      LocalRestart \/ StrayAdmission \/ Heal \/ ResumeRequest \/ Service)
 /\ staged'=run'
 /\ UNCHANGED <<service,delivered>>
PressureDone == Done /\ service.active="" /\ run=staged
OtherWork == /\ ~PressureDone /\ unrelated'=~unrelated /\ UNCHANGED <<run,staged,service,delivered>>
PressureNext == Start \/ Backend \/ CancelObserver \/ Normal \/ OtherWork \/
 (PressureDone /\ UNCHANGED allvars)
PressureSpec == PressureInit /\ [][PressureNext]_allvars /\
 WF_allvars(Start) /\ WF_allvars(Backend) /\ WF_allvars(Normal) /\ WF_allvars(OtherWork)
PressureCompletes == <>PressureDone
PhysicalLifetime == S!PhysicalLifetime(service)
CapacityBound == S!Bounded(PressureBug,service) /\ S!OrdinarySaturated(service)
DeliveredFromCompletion == \A e \in delivered:e.id \in service.completed /\ e.payload=service.captured[e.id]
NoPressureSOS == ~(run.exports#{} /\ service.completed#{} /\ run.blocked)
=============================================================================
