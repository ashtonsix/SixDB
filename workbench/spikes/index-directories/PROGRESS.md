# Current experiment coverage

The active objective is to run the index-directory spike described in
[README.md](README.md), preserving the supplied five-layer model. Completing
one performance panel does not finish that scope. Evidence below describes
implemented experiments; open rows remain work, not waived requirements.

| Planned question | Current evidence | Work still required |
| --- | --- | --- |
| Table/hash/Boundlist composition, first overlap and same-layer continuation | `composed`, `pressure`; independent record-derived layer oracle; J=0/2, duplicate/open-bound/gap cases | Full generic byte-key directory composition; wider J and presence/link alternatives where they change the result. |
| Directory storage and interior-edit scaling | Flat, prefix-grid and occupied-entry pages; internal branch and row-chunk controls; three native targets | Stable split/merge maintenance and populated-page growth; current occupied-page structural edits repartition the roster. |
| Hash geometry, occupancy and overflow | Typed whole/page controls; actual spill counts and rebuild costs | Growth through maintained directory histories, rather than rebuilding every owner. |
| Search binding, long keys and capacity endpoints | `baseline`, `keys`; numeric binding, arbitrary byte comparisons, UINT_MAX sentinel and virtual 2^32 outer coordinates | Binding amortization across real directory versions; long keys in the complete Object-backed path; above-cache qualification. |
| 64-byte dirent and locator/interpretation coherence | Six 8-byte locators and explicit 16-byte metadata layout; actual Orbital MVCC outside it; raw/full-cell/compact-cell comparison on three native targets | Mapped/resource-ID recipes and portable reconstruction. |
| Schema diversity and migration | `schema`: selected segments at every layer migrate across two codecs and differing chunk/auxiliary-Part counts; old/unmigrated interpretations remain valid; definition lookup and conversion measured from 32 to 100,000 definitions on three native targets; native construction rollback covered by `failure` | Portable schema reconstruction and mapped/durable conversion failure remain with the persistence row. |
| Parts, overflow and relocation | Actual Allocation/Representation/Part ownership; StreamPack blocks/ranks; retained remaps; selected Part bytes and complete cell/geometry proof survive Index teardown | Colocated ownership controls and escape-retention economics; failure-safe relocation/reopen. |
| Ordered physical placement, duplicates and padding | Shift, hierarchical redistribution and midpoint controls; boundary duplicate move; small-domain exhaustion; actual five-layer publication | Read/scan effects through sustained churn, prepend/consolidation and a chunked/gapped payload control. |
| Aggregate damage and empty/nonempty transitions | Exact integer/null/count oracle at every layer; deletion/recreation histories; direct replacement deltas and repeated/last-extremum-witness, all-null and unchanged-value histories with rollback | More varied value damage and payload movement must accompany structural maintenance. Reuse the aggregate-maintenance conclusions when pricing eager versus buffered work with corrected reads, full draining and retained snapshots. |
| Atomic edit failure, refusal, abort and retry | `failure`: reversible author edits; injected construction/accepted-abort checkpoints; actual metadata/journal allocation refusal; actual stale-basis publication; identical-request retries with retained readers | The tested synchronous writer does not establish concurrent author rebasing or mapped/durable failure recovery; resource-wait and persistence rows remain open. |
| Execution, waits and source lifetime | Actual Loom Ring, stationary Scopes through publication, active/overflow completion/discard | A resource wait using actual Tasks/Signals; bound-batch preparation/drain and escaped-Part histories on that path. |
| Cached locations, plans and cursors | Snapshot reads and moved-key correctness | Explicit cache/cursor invalidation histories across relabel, page rank and schema changes. |
| Persistence and external resolution | Changed-address semantic rebuild; source/provenance archives | Portable byte reopen, mapped/opaque locators and explicit durable-update dependencies; a rebuild is not a reopen or recovery proof. |
| Prior art and future Engine comparison | `PRIOR-ART.md`, Calico source/campaign review, `ENGINE-VS-LMDB.md` | Reconcile new evidence into recommendations. A full Engine-versus-LMDB driver remains a separate proposal. |

The [memory-locator panel](LOCATORS.md) now measures raw addresses against full
and compact immutable reference cells resolved against the selected segment.
Its three-target evidence includes the read penalty, complete-update savings,
cell metadata, removal/recreation and escaping-Part histories. Value updates
still publish actual Orbital versions while leaving the directory unchanged.
This completes that native-memory experiment, not the mapped/resource locator
or persistence questions, and changes neither segment identity nor layer ordering.

The [schema panel](SCHEMA.md) now covers explicit migration and historical-tail
lookup costs. Native-source conversion shares unchanged columns and outperforms
full author-model reconstruction; the latter remains in the measured panel.
Full-copy schema extension is still linear in definition population and its
cost is reported. This completes the bounded schema experiment, while the
cross-cutting persistence, resource suspension and general-key work above remains active.

The [failure panel](FAILURES.md) adds fallible editor paths with allocation-free
reverse replay. Its histories include first population, insertion/removal,
complete removal, schema extension and migration. The initial shared-Pool
measurement panel exposed capture-cost regimes despite matching logical work;
the follow-up isolates the Worker/Pool/Arena per fixture. Retained shared-state
measurements remain pressure evidence, not an undo-speedup claim. Persistence,
resource suspension, general keys and bounded directory maintenance remain open.
The final direct-replacement control cuts inverse records to roughly 0.5 KB
without changing native bytes or five-layer publication. All three native
targets pass the expanded failure histories; all nine local drivers pass
ASan/UBSan. This removes unnecessary author work, while the larger copied-chunk,
structural and maintenance-policy questions remain in scope.

Earlier committed measurements are in [FINDINGS.md](FINDINGS.md) and
[evidence](evidence/README.md). Current source and native runs must be checked
before treating a row as finished. The goal remains active while the listed
implementation questions lack direct evidence.
