---------------------------- MODULE JournalSchedule ----------------------------
EXTENDS Contracts
\* Deterministic normal service between explicitly authored causal cuts.

Priority(tag) ==
  CASE tag="input.tx.fact" -> 0
    [] tag="input.journal.deliver" -> 1
    [] tag="input.journal.snapshot" -> 1
    [] tag="journal.deliver" -> 2
    [] tag="journal.snapshot" -> 2
    [] tag="log.deliver" -> 2
    [] tag="log.snapshot" -> 2
    [] tag="journal.learn" -> 3
    [] tag="journal.evidence.receive" -> 4
    [] tag="journal.accept.reply" -> 4
    [] tag="journal.accept.persist" -> 5
    [] tag="journal.accept.submit" -> 6
    [] tag="journal.accept.self.submit" -> 7
    [] tag="journal.prepared" -> 8
    [] tag="journal.promise.receive" -> 9
    [] tag="journal.promise.reply" -> 10
    [] tag="journal.promise.persist" -> 11
    [] tag="journal.promise.submit" -> 12
    [] tag="journal.prepare" -> 13
    [] tag="journal.append" -> 14
    [] tag="input.journal.submit" -> 15
    [] OTHER -> 20
Choose(steps) == CHOOSE x \in steps: \A y \in steps:Priority(x.tag)<=Priority(y.tag)
=============================================================================
