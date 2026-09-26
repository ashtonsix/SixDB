# Broad protection: the question has expanded

Ashton's largest-value example reopened retained C1 protection: a coherent read,
an atomic serializable select-and-act, and a promise that the live maximum stays
unchanged impose different requirements. Treating all of them as long-lived
exclusion can connect many otherwise independent small transactions.

The [current reconsideration](reconsideration/README.md) extends that challenge to
large writes, SQL/HTAP/ETL workloads and preparation itself. It compares whole
mechanisms by what they remove and which costs they accept. Separating read
versions, dependencies and exclusion is useful analysis; it is not a selected
replacement architecture or a reason to add another protocol layer.

The [read scenarios](reconsideration/READS.md) retain the maximum example and its
serialization-cycle variation. The [write scenarios](reconsideration/WRITES.md)
examine ordered bulk programs, private generations and lost-update counterexamples.
[Prior art](reconsideration/PRIOR-ART.md) and the
[finite history probe](reconsideration/histories.py) support the comparison.
