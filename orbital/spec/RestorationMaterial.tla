---------------------------- MODULE RestorationMaterial ----------------------------
EXTENDS RecoveryCut
R == INSTANCE RecoveryKernel
M == INSTANCE MaterialCore
HolderSet == {11,12}
RootSet == {"read","redundancy"}
RP == [roots |-> RootSet,holders |-> HolderSet,data |-> [t \in {} |-> t],
       initial |-> [h \in HolderSet |-> {}],reset |-> FALSE,gc |-> FALSE,
       abort |-> FALSE,bad |-> "none",retries |-> 1,owner |-> 7,actor |-> "root-owner"]
RJP == [owners |-> {7},actors |-> {"root-owner"},subscribers |-> [a \in {7}|->{"root-owner"}],
        initialConfig |-> [a \in {7}|->1]]
RestoredRecipe == [id |-> "restored-image",base |-> "restored-base",code |-> "restored-code",
                  patches |-> <<>>,interpretation |-> "checkpoint-v1"]
RestoredContext == <<"restored",cut.rebuilt.lineage,cut.rebuilt.cut>>
Definition(r) == [root |-> r,holders |-> IF r="read" THEN {11} ELSE HolderSet,
  recipe |-> RestoredRecipe,cut |-> cut.rebuilt.cut,context |-> RestoredContext,
  generation |-> 1,requester |-> RestoreActor,view |-> r,waitForMaterial |-> TRUE,
  rights |-> [read |-> {1,2},write |-> {}],successor |-> "",predecessor |-> ""]
VARIABLES roots,rootJournal,rootNet,seeded,redundancyRequested,grants,materials,advertised
allvars == <<vars,roots,rootJournal,rootNet,seeded,redundancyRequested,grants,materials,advertised>>
JoinedInit == Init /\ roots=R!Init(RP) /\ rootJournal=J!Init(RJP) /\ rootNet={}
 /\ seeded=FALSE /\ redundancyRequested=FALSE /\ grants={} /\ materials={} /\ advertised=FALSE

(* The actual C9 reconstruction is the input: both image bytes and the decoder
   read from its retained checkpoint. Import submits a real backend copy; no
   physical holder is initialized with the expected answer. *)
Seed == /\ cut.rebuilt#TxNone /\ ~seeded
 /\ LET bytes==[kind |-> "base",interpretation |-> "checkpoint-v1",content |-> cut.rebuilt.image]
        data==[t \in {"restored-base","restored-code"}|->IF t="restored-base" THEN bytes ELSE cut.data["checkpoint-decoder"]]
    IN rootNet'=rootNet \cup
       {Event(<<"import",t>>,RestoreActor,"root-owner","root.import",
          [holder |-> 11,copy |-> R!Copy(11,t,1,data[t])]):t \in DOMAIN data} \cup
       {Event("read-root",RestoreActor,"root-owner","root.acquire",Definition("read"))}
 /\ seeded'=TRUE
 /\ UNCHANGED <<vars,roots,rootJournal,redundancyRequested,grants,materials,advertised>>
Have(r) == cut.rebuilt#TxNone /\ \E g \in grants:\E m \in materials:
 g.root=r /\ g.view=r /\ m.root=r /\ g.recipe=m.recipe /\ g.context=RestoredContext /\
 g.c=cut.rebuilt.cut /\ g.generation=1 /\ g.interpretation=m.interpretation /\
 g.interpretation=RestoredRecipe.interpretation
Readable == Have("read")
RequestRedundancy == /\ cut.opened /\ ~redundancyRequested
 /\ rootNet'=rootNet \cup {Event("redundancy-root",RestoreActor,"root-owner","root.acquire",Definition("redundancy"))}
 /\ redundancyRequested'=TRUE
 /\ UNCHANGED <<vars,roots,rootJournal,seeded,grants,materials,advertised>>
ClaimRedundancy == /\ ~advertised
 /\ (IF Bug="claim-on-submit" THEN roots.copied#{} ELSE Have("redundancy"))
 /\ advertised'=TRUE
 /\ UNCHANGED <<vars,roots,rootJournal,rootNet,seeded,redundancyRequested,grants,materials>>
RootInput(e) ==
 IF e.kind \in {"journal.submit","journal.recover"}
 THEN {Transition("input.root-journal",[j |-> tr.next,r |-> roots,
      net |-> (rootNet \ {e}) \cup Elements(tr.emissions),g |-> grants,m |-> materials],<<>>):
        tr \in J!Receive(RJP,rootJournal,e)}
 ELSE IF e.kind="ViewGrant"
 THEN {Transition("input.grant",[j |-> rootJournal,r |-> roots,net |-> rootNet \ {e},g |-> grants \cup {e.body},m |-> materials],<<>>)}
 ELSE IF e.kind="Material"
 THEN {Transition("input.material",[j |-> rootJournal,r |-> roots,net |-> rootNet \ {e},g |-> grants,m |-> materials \cup {e.body}],<<>>)}
 ELSE {Transition("input.root",[j |-> rootJournal,r |-> tr.next,
       net |-> (rootNet \ {e}) \cup Elements(tr.emissions),g |-> grants,m |-> materials],<<>>):
         tr \in R!Receive(RP,roots,e)}
Inputs == UNION {RootInput(e):e \in rootNet}
RootActions == {tr \in R!Actions(RP,roots):
 tr.tag#"copy-immutable-material" \/ (cut.opened /\
   \E key \in tr.next.copied \ roots.copied:key[1]=11 /\ key[2]=12)}
RootService ==
 /\ IF Inputs#{}
    THEN LET tr==CHOOSE x \in Inputs:TRUE IN
         /\ roots'=tr.next.r /\ rootJournal'=tr.next.j /\ rootNet'=tr.next.net
         /\ grants'=tr.next.g /\ materials'=tr.next.m
    ELSE IF J!Actions(RJP,rootJournal)#{}
         THEN LET tr==CHOOSE x \in J!Actions(RJP,rootJournal):TRUE IN
              /\ rootJournal'=tr.next /\ rootNet'=rootNet \cup Elements(tr.emissions)
              /\ UNCHANGED <<roots,grants,materials>>
         ELSE /\ RootActions#{}
              /\ LET tr==CHOOSE x \in RootActions:TRUE IN
                   /\ roots'=tr.next /\ rootNet'=rootNet \cup Elements(tr.emissions)
              /\ UNCHANGED <<rootJournal,grants,materials>>
 /\ UNCHANGED <<vars,seeded,redundancyRequested,advertised>>
RecoveryLocal == TxStep \/ Offer \/ Continue \/ Disaster \/ Rebuild \/ OldWrite \/
  (Readable /\ ExternalFence) \/
  (\E tr \in {x \in C!Actions(CP,cut):x.tag#"cut.open" \/ Readable \/ Bug="open-before-material"}:
    /\ cut'=tr.next /\ network'=network \cup Elements(tr.emissions)
    /\ UNCHANGED <<tx,journal,phase,restored,externalDead,oldTried>>)
RecoveryStep ==
 /\ (Deliver \/ (~ENABLED Deliver /\ JournalStep) \/
      (~ENABLED Deliver /\ ~ENABLED JournalStep /\ RecoveryLocal))
 /\ UNCHANGED <<roots,rootJournal,rootNet,seeded,redundancyRequested,grants,materials,advertised>>
LocalJoined == Seed \/ RequestRedundancy \/ RecoveryStep
JoinedService == ClaimRedundancy \/ RootService \/ (~ENABLED RootService /\ LocalJoined)
JoinedDone == Done /\ advertised /\ rootNet={}
JoinedNext == JoinedService \/ (JoinedDone /\ UNCHANGED allvars)
JoinedSpec == JoinedInit /\ [][JoinedNext]_allvars /\ WF_allvars(JoinedService)
JoinedCompletes == <>JoinedDone
ReadBeforeWrite == cut.opened => Readable
MaterialMeaning == \A m \in materials:
 m.bytes=(IF Mode="normal" THEN Image(tx) ELSE ExpectedCutImage) /\
 m.interpretation=RestoredRecipe.interpretation /\ m.recipe=RestoredRecipe.id
PhysicalClosure == R!HeldExists(RP,roots) /\ R!LiveRetained(RP,roots)
ActualRedundancy == advertised =>
 /\ roots.roots["redundancy"].phase="live"
 /\ \A h \in HolderSet:
     /\ roots.holds[h]["redundancy"].phase="held"
     /\ M!WellTyped(R!HeldData(roots,h,"redundancy"),RestoredRecipe)
     /\ M!Reconstruct(R!HeldData(roots,h,"redundancy"),RestoredRecipe)=
          (IF Mode="normal" THEN Image(tx) ELSE ExpectedCutImage)
NoReadOnlyMilestone == ~(Readable /\ ~cut.opened)
NoWritableDegraded == ~(cut.opened /\ ~advertised /\ DOMAIN roots.copies[12]={})
NoRepaired == ~JoinedDone
=============================================================================
