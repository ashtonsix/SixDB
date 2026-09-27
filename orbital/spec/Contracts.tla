------------------------------ MODULE Contracts ------------------------------
EXTENDS Naturals, FiniteSets, Sequences, TLC

\* Shared wire vocabulary for the executable models, not a production ABI.
Command(owner, id, kind, body) ==
    [owner |-> owner, id |-> ToString(id), kind |-> kind, body |-> body]

Event(id, src, dst, kind, body) ==
    [id |-> ToString(id), src |-> src, dst |-> dst, kind |-> kind, body |-> body]

Transition(tag, next, emissions) ==
    [tag |-> tag, next |-> next, emissions |-> emissions]

Prefix(a, b) == Len(a) <= Len(b) /\ a = SubSeq(b, 1, Len(a))
Comparable(a, b) == Prefix(a, b) \/ Prefix(b, a)
Prefixes(a) == {SubSeq(a, 1, n) : n \in 0..Len(a)}
Elements(a) == {a[i] : i \in 1..Len(a)}
SuffixAfter(a, n) == SubSeq(a, n + 1, Len(a))
None == "none"

=============================================================================
