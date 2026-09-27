-------------------------- MODULE BootstrapUnderPressure ------------------------
EXTENDS BootstrapDiscovery
CONSTANT PressureBug
VARIABLES staged,service,noise,delivered
B == INSTANCE BootstrapDiscovery WITH run <- staged
S == INSTANCE RecoveryServiceKernel
allvars == <<run,staged,service,noise,delivered>>
PressureInit == Init /\ staged=run /\ service=S!Init /\ noise=FALSE /\ delivered={}
SnapshotInput == \E e \in staged.network:
 /\ e.kind="journal.snapshot" /\ e.dst="bootstrap"
 /\ \E tr \in B!Input(e):staged'=tr.next
NamespaceChoices == {q \in {1,2} \X Roots:ENABLED B!ExportNamespace(q[1],q[2])}
CopyChoices == UNION {{<<h,id>>:id \in {x \in DOMAIN staged.roots.copies[h]:ENABLED B!ExportCopy(h,x)}}:h \in {1,2}}
ClassifyChoices == {r \in DOMAIN staged.definitions:ENABLED B!Classify(r)}
Capture == IF ENABLED B!InitialQuery THEN B!InitialQuery
 ELSE IF ENABLED B!LateQuery THEN B!LateQuery
 ELSE IF ENABLED SnapshotInput THEN SnapshotInput
 ELSE IF NamespaceChoices#{} THEN LET q==CHOOSE x \in NamespaceChoices:TRUE IN B!ExportNamespace(q[1],q[2])
 ELSE IF CopyChoices#{} THEN LET q==CHOOSE x \in CopyChoices:TRUE IN B!ExportCopy(q[1],q[2])
 ELSE IF ClassifyChoices#{} THEN B!Classify(CHOOSE r \in ClassifyChoices:TRUE)
 ELSE FALSE

Start ==
 /\ service.active="" /\ staged=run /\ Capture
 /\ LET payload==[prefix|->staged'.prefix,copies|->staged'.copies \ run.copies,
       holds|->staged'.holds \ run.holds,ready|->staged'.ready \ run.ready,
       exports|->staged'.exports \ run.exports,requests|->staged'.ledger.pending \ run.ledger.pending]
    IN \E tr \in S!Begin(PressureBug,service,payload):service'=tr.next
 /\ UNCHANGED <<run,noise,delivered>>
Backend == \E tr \in S!Steps(PressureBug,service):
 /\ service'=tr.next
 /\ IF tr.emissions= <<>> THEN UNCHANGED <<run,delivered>>
    ELSE /\ run'=staged
         /\ delivered'=delivered \cup {Head(tr.emissions).body}
 /\ UNCHANGED <<staged,noise>>
CancelObserver == \E tr \in S!Cancel(service):
 /\ service'=tr.next /\ UNCHANGED <<run,staged,noise,delivered>>
OrdinarySteps == {tr \in Steps:tr.tag#"input.journal.snapshot"}
OrdinaryService == /\ OrdinarySteps#{} /\ run'=Choose(OrdinarySteps).next
Normal ==
 /\ service.active=""
 /\ (Initial \/ Late \/ RestartObserver \/ Partial \/ Activate \/ OrdinaryService)
 /\ staged'=run'
 /\ UNCHANGED <<service,noise,delivered>>
PressureDone == Done /\ service.active="" /\ run=staged
OtherWork == /\ ~PressureDone /\ noise'=~noise /\ UNCHANGED <<run,staged,service,delivered>>
Urgent == /\ service.active="" /\ (Initial \/ Late \/ RestartObserver \/ Partial)
 /\ staged'=run' /\ UNCHANGED <<service,noise,delivered>>
PressureNext == IF ENABLED Urgent THEN Urgent
 ELSE IF ENABLED Start THEN Start ELSE IF ENABLED Normal THEN Normal
 ELSE Backend \/ CancelObserver \/ OtherWork \/
 (PressureDone /\ UNCHANGED allvars)
PressureSpec == PressureInit /\ [][PressureNext]_allvars /\
 WF_allvars(Start) /\ WF_allvars(Backend) /\ WF_allvars(Normal) /\ WF_allvars(OtherWork)
PressureCompletes == <>PressureDone
PhysicalLifetime == S!PhysicalLifetime(service)
CapacityBound == S!Bounded(PressureBug,service) /\ S!OrdinarySaturated(service)
DeliveredFromCompletion == \A e \in delivered:e.id \in service.completed /\ e.payload=service.captured[e.id]
NoPressureCompletion == ~PressureDone
=============================================================================
