------------------------ MODULE JournalOrderContract -------------------------
EXTENDS Contracts
CONSTANT JournalNone
\* Independent correspondence checks at the concrete provider's observable
\* boundaries. Private queues/callback scheduling are stuttering; the compact
\* order model admits a durable acceptor step and arbitrary phase-one quorum.
Full(s,c,v) ==
 (IF s.disk[c][v].base=JournalNone THEN <<>>
  ELSE (CHOOSE x \in s.blobs:x.id=s.disk[c][v].base).seq) \o s.disk[c][v].tail
PrefixProposal(s,c,b,seq) == [cfg|->c,ballot|->b,seq|->seq] \in s.proposalHistory
Closed(c,seq) == \E i \in 1..Len(seq):seq[i].kind="journal.handoff" /\ seq[i].body.source=c
BestReport(rs,seq) ==
 \E best \in rs:
  /\ best.seq=seq
  /\ \A r \in rs:best.ab>r.ab \/ (best.ab=r.ab /\ Len(best.seq)>=Len(r.seq))
DiskStep(p,old,new,c,v) ==
 IF v \in new.destroyed THEN ~new.disk[c][v].initialized
 ELSE IF ~old.disk[c][v].initialized /\ new.disk[c][v].initialized
 THEN \E m \in old.net:
       /\ m.kind="initialize" /\ m.dst=v /\ m.body.cfg=c
       /\ LET seq==m.body.value.seq
          IN /\ Len(seq)>0 /\ seq[Len(seq)].kind="journal.handoff"
             /\ seq[Len(seq)].body.target=c /\ Full(new,c,v)=seq
 ELSE IF old.disk[c][v].initialized /\ ~new.disk[c][v].initialized THEN FALSE
 ELSE /\ new.disk[c][v].promise>=old.disk[c][v].promise
      /\ IF new.disk[c][v].ab=old.disk[c][v].ab /\ Full(new,c,v)=Full(old,c,v)
         THEN TRUE
         ELSE /\ new.disk[c][v].ab>=old.disk[c][v].promise
              /\ new.disk[c][v].promise=new.disk[c][v].ab
              /\ PrefixProposal(old,c,new.disk[c][v].ab,Full(new,c,v))
              /\ new.disk[c][v].ab#old.disk[c][v].ab \/ Prefix(Full(old,c,v),Full(new,c,v))
SelectionStep(p,old,new,c,b) ==
 IF old.leaders[c][b].phase="prepare" /\ new.leaders[c][b].phase="ready"
 THEN /\ Cardinality({r.voter:r \in old.leaders[c][b].replies})*2>Cardinality(p.members[c])
      /\ BestReport(old.leaders[c][b].replies,new.leaders[c][b].seq)
 ELSE TRUE
Corresponds(p,old,new) ==
 /\ \A c \in p.configs:\A v \in p.members[c]:DiskStep(p,old,new,c,v)
 /\ \A c \in p.configs,b \in p.ballots:SelectionStep(p,old,new,c,b)
=============================================================================
