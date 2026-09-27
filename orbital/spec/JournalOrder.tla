----------------------------- MODULE JournalOrder -----------------------------
EXTENDS Contracts
CONSTANTS Mode, Ballots, Entries, Transfers, Topology, Compaction, Destruction
\* Logical quorum-order abstraction. Physical command serialization and reply
\* origin are checked by JournalPersistence; concrete ingress, follower learning,
\* and actor replay use JournalKernel. No node reads the chosen-history observer.
C == 0..Transfers
B == 1..Ballots
Members(c) == IF Topology="overlap"
  THEN IF c=0 THEN {"A","B","C"} ELSE IF c=1 THEN {"A","B","D"} ELSE {"B","D","E"}
  ELSE IF c=0 THEN {"A","B","C"} ELSE IF c=1 THEN {"D","E","F"} ELSE {"G","H","I"}
Leader(c,b) == IF Topology="overlap"
  THEN IF c=0 THEN <<"A","B","C">>[b] ELSE IF c=1 THEN <<"A","B","D">>[b] ELSE <<"B","D","E">>[b]
  ELSE IF c=0 THEN <<"A","B","C">>[b] ELSE IF c=1 THEN <<"D","E","F">>[b] ELSE <<"G","H","I">>[b]
Voters == UNION {Members(c):c \in C}
Value(i) == [kind|->"value",value|->i,source|->0,target|->0]
Stop(c,target) == [kind|->"stop",value|->0,source|->c,target|->target]
Values == {Value(i):i \in 1..Entries}
Stops(c,seq) == {i \in 1..Len(seq):seq[i].kind="stop" /\ seq[i].source=c}
Closed(c,seq) == Stops(c,seq)#{}
Vote(c,b,v,seq) == [cfg|->c,ballot|->b,voter|->v,seq|->seq]
Quorum(votes,c,b,seq) == Cardinality({r.voter:r \in {x \in votes:
  x.cfg=c /\ x.ballot=b /\ x.voter \in Members(c) /\ Prefix(seq,x.seq)}})*2>Cardinality(Members(c))
VARIABLES promise, acceptedBallot, base, tail, initialized, prepared, dead,
          replies, proposal, floor, sent, acknowledgements, certificates, history, chosen,
          destroyed, compacted
vars == <<promise,acceptedBallot,base,tail,initialized,prepared,dead,replies,
          proposal,floor,sent,acknowledgements,certificates,history,chosen,destroyed,compacted>>
Full(c,v) == base[c][v] \o tail[c][v]
Init ==
 /\ promise=[c \in C|->[v \in Members(c)|->0]]
 /\ acceptedBallot=[c \in C|->[v \in Members(c)|->0]]
 /\ base=[c \in C|->[v \in Members(c)|-><<>>]] /\ tail=base
 /\ initialized=[c \in C|->[v \in Members(c)|->c=0]]
 /\ prepared=[c \in C|->[b \in B|->FALSE]] /\ dead={}
 /\ replies=[c \in C|->[b \in B|->{}]]
 /\ proposal=[c \in C|->[b \in B|-><<>>]]
 /\ floor=[c \in C|->[b \in B|->0]]
 /\ sent={} /\ acknowledgements={} /\ certificates={}
 /\ history={} /\ chosen={} /\ destroyed={} /\ compacted={}

Prepare(c,b,v) ==
 /\ initialized[c][v] /\ v \notin destroyed
 /\ (initialized[c][Leader(c,b)] \/ prepared[c][b])
 /\ b>promise[c][v]
 /\ promise'=[promise EXCEPT ![c][v]=IF Mode="volatile-promise" THEN @ ELSE b]
 /\ replies'=[replies EXCEPT ![c][b]=IF prepared[c][b] THEN @ ELSE @ \cup
       {[voter|->v,ballot|->acceptedBallot[c][v],seq|->Full(c,v)]}]
 /\ UNCHANGED <<acceptedBallot,base,tail,initialized,prepared,dead,proposal,floor,sent,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
Recover(c,b,rs) ==
 /\ rs \subseteq replies[c][b]
 /\ ~prepared[c][b] /\ <<c,b>> \notin dead
 /\ Cardinality({r.voter:r \in rs})*2>Cardinality(Members(c))
 /\ LET best==IF Mode="wrong-recovery"
        THEN CHOOSE r \in rs:\A x \in rs:
             r.ballot<x.ballot \/ (r.ballot=x.ballot /\ Len(r.seq)<=Len(x.seq))
        ELSE CHOOSE r \in rs:\A x \in rs:
             r.ballot>x.ballot \/ (r.ballot=x.ballot /\ Len(r.seq)>=Len(x.seq))
    IN /\ proposal'=[proposal EXCEPT ![c][b]=best.seq]
       /\ floor'=[floor EXCEPT ![c][b]=Len(best.seq)]
       /\ prepared'=[prepared EXCEPT ![c][b]=TRUE]
       /\ replies'=[replies EXCEPT ![c][b]={}]
 /\ UNCHANGED <<promise,acceptedBallot,base,tail,initialized,dead,sent,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
Extend(c,b,value) ==
 /\ prepared[c][b] /\ <<c,b>> \notin dead
 /\ value \notin Elements(proposal[c][b])
 /\ ~Closed(c,proposal[c][b]) \/ Mode="extend-terminal"
 /\ proposal'=[proposal EXCEPT ![c][b]=Append(@,value)]
 /\ UNCHANGED <<promise,acceptedBallot,base,tail,initialized,prepared,dead,replies,floor,sent,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
AppendStop(c,b,t) ==
 /\ prepared[c][b] /\ <<c,b>> \notin dead /\ t>c /\ Len(proposal[c][b])>0
 /\ ~Closed(c,proposal[c][b])
 /\ proposal'=[proposal EXCEPT ![c][b]=Append(@,Stop(c,t))]
 /\ UNCHANGED <<promise,acceptedBallot,base,tail,initialized,prepared,dead,replies,floor,sent,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
Send(c,b) ==
 /\ prepared[c][b] /\ <<c,b>> \notin dead /\ Len(proposal[c][b])>0
 /\ LET message==[cfg|->c,ballot|->b,seq|->proposal[c][b],minimum|->floor[c][b]]
    IN /\ message \notin sent
       /\ sent'={old \in sent:~(old.cfg=c /\ old.ballot=b /\ Prefix(old.seq,message.seq))} \cup {message}
 /\ UNCHANGED <<promise,acceptedBallot,base,tail,initialized,prepared,dead,replies,proposal,floor,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
Accept(m,v) ==
 /\ v \in Members(m.cfg) /\ v \notin destroyed /\ initialized[m.cfg][v]
 /\ m.ballot>=promise[m.cfg][v]
 /\ acceptedBallot[m.cfg][v]#m.ballot \/ Prefix(Full(m.cfg,v),m.seq)
 /\ LET c==m.cfg b==m.ballot vote==Vote(c,b,v,m.seq)
        h=={r \in history:~(r.cfg=c /\ r.ballot=b /\ r.voter=v /\ Prefix(r.seq,m.seq))} \cup {vote}
        q==UNION {{[cfg|->r.cfg,ballot|->r.ballot,seq|->seq]:
              seq \in {s \in Prefixes(r.seq):Len(s)>0 /\ Quorum(h,r.cfg,r.ballot,s)}}:r \in h}
    IN /\ vote \notin acknowledgements
       /\ promise'=[promise EXCEPT ![c][v]=b]
       /\ acceptedBallot'=[acceptedBallot EXCEPT ![c][v]=b]
       /\ base'=[base EXCEPT ![c][v]= <<>>] /\ tail'=[tail EXCEPT ![c][v]=m.seq]
       /\ acknowledgements'=h
       /\ history'=h /\ chosen'=chosen \cup q
 /\ UNCHANGED <<initialized,prepared,dead,replies,proposal,floor,sent,certificates,destroyed,compacted>>
Certify(m) ==
 /\ Quorum(acknowledgements,m.cfg,m.ballot,m.seq)
 /\ ~(\E old \in certificates:old.cfg=m.cfg /\ old.ballot=m.ballot /\ Prefix(m.seq,old.seq))
 /\ certificates'={old \in certificates:~(old.cfg=m.cfg /\ old.ballot=m.ballot /\ Prefix(old.seq,m.seq))} \cup {m}
 /\ UNCHANGED <<promise,acceptedBallot,base,tail,initialized,prepared,dead,replies,proposal,floor,sent,
                acknowledgements,history,chosen,destroyed,compacted>>
Initialize(c,v,cert) ==
 /\ v \notin destroyed /\ ~initialized[c][v] /\ Closed(cert.cfg,cert.seq)
 /\ cert.seq[Len(cert.seq)].target=c \/ (Mode="wrong-successor" /\ c>cert.cfg)
 /\ initialized'=[initialized EXCEPT ![c][v]=TRUE]
 /\ base'=[base EXCEPT ![c][v]=cert.seq]
 /\ UNCHANGED <<promise,acceptedBallot,tail,prepared,dead,replies,proposal,floor,sent,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
Reset(c,b) ==
 /\ Destruction /\ b=1 /\ prepared[c][b] /\ <<c,b>> \notin dead
 /\ dead'=dead \cup {<<c,b>>}
 /\ UNCHANGED <<promise,acceptedBallot,base,tail,initialized,prepared,replies,proposal,floor,sent,
                acknowledgements,certificates,history,chosen,destroyed,compacted>>
Destroy(v) ==
 /\ Destruction /\ destroyed={} /\ v="A"
 /\ \E r \in history:r.voter=v
 /\ destroyed'={v}
 /\ promise'=[c \in C|->[n \in Members(c)|->IF n=v THEN 0 ELSE promise[c][n]]]
 /\ acceptedBallot'=[c \in C|->[n \in Members(c)|->IF n=v THEN 0 ELSE acceptedBallot[c][n]]]
 /\ base'=[c \in C|->[n \in Members(c)|->IF n=v THEN <<>> ELSE base[c][n]]]
 /\ tail'=[c \in C|->[n \in Members(c)|->IF n=v THEN <<>> ELSE tail[c][n]]]
 /\ initialized'=[c \in C|->[n \in Members(c)|->IF n=v THEN FALSE ELSE initialized[c][n]]]
 /\ UNCHANGED <<prepared,dead,replies,proposal,floor,sent,acknowledgements,certificates,history,chosen,compacted>>
Compact(c,v,cert) ==
 /\ Compaction /\ v \notin destroyed /\ initialized[c][v]
 /\ Prefix(cert.seq,Full(c,v)) /\ Len(cert.seq)>Len(base[c][v])
 /\ base'=[base EXCEPT ![c][v]=cert.seq]
 /\ tail'=[tail EXCEPT ![c][v]=IF Mode="discard-suffix" THEN <<>> ELSE SuffixAfter(Full(c,v),Len(cert.seq))]
 /\ compacted'=compacted \cup {<<c,v,Len(cert.seq)>>}
 /\ UNCHANGED <<promise,acceptedBallot,initialized,prepared,dead,replies,proposal,floor,sent,
                acknowledgements,certificates,history,chosen,destroyed>>
Protocol ==
 (\E c \in C:\E b \in B,v \in Members(c):Prepare(c,b,v)) \/
 (\E c \in C,b \in B:(\E rs \in SUBSET replies[c][b]:Recover(c,b,rs)) \/ Send(c,b) \/ Reset(c,b) \/
      (\E x \in Values:Extend(c,b,x)) \/ (\E target \in C:AppendStop(c,b,target))) \/
 (\E m \in sent:\E seq \in Prefixes(m.seq) \ {<<>>}:
      (Len(seq)>=m.minimum /\ (\E v \in Members(m.cfg):Accept([m EXCEPT !.seq=seq],v))) \/ Certify([m EXCEPT !.seq=seq])) \/
 (\E c \in C:\E v \in Members(c),cap \in certificates:
      \E seq \in Prefixes(cap.seq) \ {<<>>}:
        Initialize(c,v,[cap EXCEPT !.seq=seq]) \/ Compact(c,v,[cap EXCEPT !.seq=seq])) \/
 (\E v \in Voters:Destroy(v))
\* A finite workload may intentionally stop after any legal prefix. This model
\* checks safety; concrete service models separately check progress obligations.
Next == Protocol \/ UNCHANGED vars
Spec == Init /\ [][Next]_vars
Agreement == \A x,y \in chosen:Comparable(x.seq,y.seq)
Closure == \A r \in history:\A i \in Stops(r.cfg,r.seq):i=Len(r.seq)
NamedSuccessor == \A c \in C\{0}:\A v \in Members(c):initialized[c][v] =>
  \E cert \in certificates:Closed(cert.cfg,cert.seq) /\
      cert.seq[Len(cert.seq)].target=c /\ Prefix(cert.seq,Full(c,v))
Retained == \A r \in history:(r.voter \notin destroyed /\ acceptedBallot[r.cfg][r.voter]=r.ballot) =>
   Prefix(r.seq,Full(r.cfg,r.voter))
Authentic == \A cert \in certificates:Quorum(history,cert.cfg,cert.ballot,cert.seq)
NoChosen == chosen={}
NoTransfer == ~\E r \in chosen:r.cfg>0
NoSecondTransfer == ~\E r \in chosen:Cardinality({i \in 1..Len(r.seq):r.seq[i].kind="stop"})>=2
NoCompaction == compacted={}
=============================================================================
