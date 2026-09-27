----------------------- MODULE AdmissionCoverage -----------------------
EXTENDS Naturals, FiniteSets

(* One producer stream, three failure domains, one possible permanent    *)
(* domain loss. The journal's ordered, durable frontier is IMPORTED.     *)
(* This checks the evidence supplied to that journal, not Paxos.         *)
(* Body and interpretation bytes persist separately; forwarding retains*)
(* the original holder identity. Finite message records abstract retries*)
(* and delivery duplicates without counting copies as new durability.   *)
(* Imported: stable entry identity, authentic domain attribution, idempotent
   receipt delivery. One holder per domain. copies contains immutable complete
   body+shared-decoder packages which may outlive the sender. There is no
   mailbox/routing algorithm, fetch/discovery protocol or code-version choice.
   Two historical receipts need not mean two copies ever coexisted: the fault
   budget is one loss over the WHOLE execution, not one fresh loss after each
   admission. SimultaneousCopiesAtEligibility intentionally exposes this limit. *)
CONSTANTS Entries, Bug
IDs == 1..Entries
Domains == 1..3
Pairs == IDs \X Domains
VARIABLES bodies, decoders, copies, receipts, eligible, frontier,
          failed, completeEvidence, coexisted, lostAdmitted
vars == <<bodies, decoders, copies, receipts, eligible, frontier,
          failed, completeEvidence, coexisted, lostAdmitted>>

Init ==
  /\ bodies = [d \in Domains |-> {}]
  /\ decoders = {}
  /\ copies = {<<e, 1>> : e \in IDs}
  /\ receipts = [e \in IDs |-> {}]
  /\ eligible = {}
  /\ frontier = 0
  /\ failed = {}
  /\ completeEvidence = [e \in IDs |-> {}]
  /\ coexisted = {}
  /\ lostAdmitted = {}

HasBody(d, e) == e \in bodies[d]
HasMaterial(d, e) == HasBody(d, e) /\ d \in decoders
Recoverable(e) == \E d \in Domains : HasMaterial(d, e)

Coexistence(bs, ds) == coexisted \cup
  {e \in IDs : Cardinality({d \in Domains : e \in bs[d] /\ d \in ds}) >= 2}

PersistBody(e, d) ==
  /\ <<e, d>> \in copies /\ d \notin failed /\ ~HasBody(d, e)
  /\ bodies' = [bodies EXCEPT ![d] = @ \cup {e}]
  /\ coexisted' = Coexistence([bodies EXCEPT ![d] = @ \cup {e}], decoders)
  /\ UNCHANGED <<decoders, copies, receipts, eligible, frontier, failed, completeEvidence, lostAdmitted>>

PersistDecoder(d) ==
  /\ d \notin decoders \cup failed
  /\ \E e \in IDs : <<e, d>> \in copies
  /\ decoders' = decoders \cup {d}
  /\ coexisted' = Coexistence(bodies, decoders \cup {d})
  /\ UNCHANGED <<bodies, copies, receipts, eligible, frontier, failed, completeEvidence, lostAdmitted>>

SendCopy(e, src, dst) ==
  /\ HasMaterial(src, e) /\ src \notin failed
  /\ dst \notin failed /\ <<e, dst>> \notin copies
  /\ copies' = copies \cup {<<e, dst>>}
  /\ UNCHANGED <<bodies, decoders, receipts, eligible, frontier, failed, completeEvidence, coexisted, lostAdmitted>>

(* Receipt issuance is separate from body/decoder durability. A delayed *)
(* receipt can survive its issuer. completeEvidence is observer history,*)
(* never consulted by the protocol's eligibility/admission transitions. *)
IssueReceipt(e, d) ==
  /\ d \notin receipts[e] /\ d \notin failed
  /\ IF Bug = "intent-receipt" THEN <<e, d>> \in copies ELSE HasMaterial(d, e)
  /\ receipts' = [receipts EXCEPT ![e] = @ \cup {d}]
  /\ completeEvidence' = IF HasMaterial(d, e)
       THEN [completeEvidence EXCEPT ![e] = @ \cup {d}] ELSE completeEvidence
  /\ UNCHANGED <<bodies, decoders, copies, eligible, frontier, failed, coexisted, lostAdmitted>>

Eligible(e) == Cardinality(receipts[e]) >= IF Bug = "count-relay-twice" THEN 1 ELSE 2
LearnEligibility(e) ==
  /\ e \notin eligible /\ Eligible(e)
  /\ eligible' = eligible \cup {e}
  /\ UNCHANGED <<bodies, decoders, copies, receipts, frontier, failed, completeEvidence, coexisted, lostAdmitted>>

(* The producer publishes only a contiguous eligible range. The bad     *)
(* variant confuses maximum receipt with a contiguous frontier.         *)
Admit(n) ==
  /\ n \in IDs /\ n > frontier
  /\ IF Bug = "skip-hole" THEN n \in eligible ELSE (1..n) \subseteq eligible
  /\ frontier' = n
  /\ UNCHANGED <<bodies, decoders, copies, receipts, eligible, failed, completeEvidence, coexisted, lostAdmitted>>

LoseDomain(d) ==
  /\ failed = {}
  /\ failed' = {d}
  /\ lostAdmitted' = {e \in 1..frontier : HasMaterial(d, e)}
  /\ bodies' = [bodies EXCEPT ![d] = {}]
  /\ decoders' = decoders \ {d}
  /\ UNCHANGED <<copies, receipts, eligible, frontier, completeEvidence, coexisted>>

ForgetDecoder(d) ==
  /\ Bug = "forget-decoder" /\ d \in decoders
  /\ decoders' = decoders \ {d}
  /\ UNCHANGED <<bodies, copies, receipts, eligible, frontier, failed, completeEvidence, coexisted, lostAdmitted>>

(* Authored requests can be lost before eligibility; no progress claim  *)
(* is made for them. The finite terminal predicate instead discharges   *)
(* every remaining modeled storage/receipt step and available prefix;
   requests lost before eligibility are outside admitted obligations.     *)
Quiescent ==
  /\ \A e \in IDs, d \in Domains \ failed :
       <<e, d>> \in copies => HasMaterial(d, e) /\ d \in receipts[e]
  /\ \A e \in IDs : Eligible(e) => e \in eligible
  /\ \A n \in IDs : (1..n) \subseteq eligible => n <= frontier
  /\ \A e \in IDs, src, dst \in Domains \ failed :
       HasMaterial(src, e) => <<e, dst>> \in copies
Terminal == Quiescent /\ UNCHANGED vars
Next == (\E e \in IDs, d \in Domains : PersistBody(e, d) \/ IssueReceipt(e, d))
  \/ (\E d \in Domains : PersistDecoder(d) \/ LoseDomain(d) \/ ForgetDecoder(d))
  \/ (\E e \in IDs, src, dst \in Domains : SendCopy(e, src, dst))
  \/ (\E e \in IDs : LearnEligibility(e) \/ Admit(e))
  \/ Terminal
Spec == Init /\ [][Next]_vars

TypeOK == /\ bodies \in [Domains -> SUBSET IDs] /\ decoders \subseteq Domains
          /\ copies \subseteq Pairs /\ receipts \in [IDs -> SUBSET Domains]
          /\ completeEvidence \in [IDs -> SUBSET Domains]
          /\ eligible \subseteq IDs /\ frontier \in 0..Entries
          /\ failed \subseteq Domains /\ Cardinality(failed) <= 1
ReceiptsAreDurable == \A e \in IDs : receipts[e] \subseteq completeEvidence[e]
DistinctEligibility == \A e \in eligible : Cardinality(completeEvidence[e]) >= 2
ContiguousAdmission == (1..frontier) \subseteq eligible
AdmittedRecoverable == \A e \in 1..frontier : Recoverable(e)
NoLossWitness == ~(lostAdmitted # {} /\ \A e \in lostAdmitted : Recoverable(e))
NoHoleWitness == ~(Entries >= 2 /\ 2 \in eligible /\ 1 \notin eligible /\ frontier = 0)
SimultaneousCopiesAtEligibility == eligible \subseteq coexisted
=======================================================================
