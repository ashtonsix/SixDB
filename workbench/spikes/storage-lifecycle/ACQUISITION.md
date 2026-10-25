# Managed archive acquisition prototype

This is the replacement's **managed artifact data path**, currently being
integrated through public tooling, not yet a deployed/qualified replacement for `worker.py`, `artifacts.py`, datasets or mutable builds. The
[workflow audit](AUDIT.md) and [independent growth failures](GROWTH-FAILURES.md)
showed successful ordinary fetches growing beyond configured budgets, with the
same inputs copied into many jobs and overlapping cached variants. A timer,
per-reference directory cache or another post-run scanner cannot establish
admission control. Existing format-1 artifact references and the regular/PAX
archives written by `artifacts.pack` are reused here.

`acquire.py` consumes an existing immutable v1 S3 artifact stream, with an
explicit external local archive source retained as the test adapter. It uses `store.py` for one shared
content-addressed payload store, bounded transaction staging, recovery metadata,
retirement and consumer ownership. Neither file edits the production tools.
S3 download streaming is implemented; upload, migration and whole-device
containment remain separate obligations. Promotion to the canonical tools
package and delegation by existing public entry points are in progress.

## Data path and ownership

An ordinary archive acquisition works as follows:

1. Without a complete declared member manifest, stream the archive once without writing a compressed or expanded copy. Check
   the existing reference's compressed byte count, SHA-256 and regular-file
   count. Hash every member and check any independently supplied member manifest,
   including omitted members. Validate gzip CRC/ISIZE and the complete tar trailer.
2. Choose slim, selected or full membership. Default slim acquisition omits
   `source.tar.gz`, `source.tar.zst` and large recognized compiler output; it does
   not fetch source packages merely because another job returned the same source.
   Explicit `--file` or `--full` restores those bytes when needed.
3. Coalesce chosen bytes by SHA-256, including equal bytes at different member
   names and in unrelated archives. Reserve the selected missing working set,
   metadata overlap and directory allowance under the global store lock.
   Preallocate payload staging before giving a writer to the producer. Its
   checked writer rejects writes beyond the declared size and verifies the
   complete size/hash; it also handles short writes. There is no compressed
   staging file or per-reference extracted tree.
4. Stream again, writing only missing selected content. With a complete declared
   size/hash/policy manifest, reserve from that declaration and perform one
   stream instead; every declaration must match actual bytes before publication. Every member and the
   **entire archive** must match the first pass before any staged payload is
   published. Several objects share one pending journal and atomic catalog
   transaction. Interrupted owned staging is reconciled on restart; journal
   removal follows synced object/staging cleanup.
5. A normal `run` gives the actual consumer a sealed inherited descriptor containing
   the member-name-to-CAS-path mapping, holds the store lock throughout its scope and uses the existing private PID
   namespace/parent-death guard. Returned paths have read-only mode. No permanent
   symlink/copy view is created. Mutable output remains explicit caller work and
   needs its own enforcing workspace.

Each retained object records one independent recovery descriptor: reopenable
S3 source or explicit local fixture path, unchanged v1 compressed
identity/provenance, full member name,
member size/hash and the expansion allowance. It also records a versioned digest of the verified complete member
size/hash/**selection-policy** map. The boolean `rebuildable` flag is included;
size/hash-only manifests cannot authenticate a warm default selection. The store checks the **whole independent
archive afresh** before a destructive eviction; an old successful fetch or
cached manifest alone cannot authorize deletion. Repeated selected acquisitions
use `member:<SHA-256>` cache aliases, rather than one alias/tree per job.

Recovery descriptors are understood by the ordinary store CLI too. A missing
archive-backed object can be recovered with `store.py get/use`, not just through
`acquire.py`. Keep both source modules together when freezing a test cut.

The canonical member manifest digest allows a supplied checked manifest to act
as a warm hint. A hit requires the complete manifest hash (including boolean selection policy)
and compressed v1 identity to match existing verified object provenance, every requested payload
to be resident, and its actual bytes to match size/hash. That hit performs no
backend read and retains the old proven recovery descriptor. Changing any
member's size/hash or selection-policy entry invalidates the proof, including an
unrequested member. A fresh scan also rejects an incorrect supplied policy flag.
A miss performs full fresh archive validation. No permanent per-reference manifest cache is created. Recent complete-manifest
proof digests are retained separately in the charged catalog, independent of
the per-object recovery source; acquiring B with shared bytes no longer removes
A's offline membership proof. The proof map is at most 64 entries and
`max(512, min(8192, budget // 128))` encoded bytes, with least-recently-used
retirement and further retirement under overall catalog pressure. It stores
only compressed identity and authenticated manifest digest/touch time, not a
second full member map or payload tree. It is a bounded recent hint, not an
eternal reference-history index. Callers supply the complete manifest for a
warm hit. Eviction still checks the independent recovery backend afresh; a
proof hint cannot authorize deletion.

## Local CLI

All examples run inside Linux (prefix them with `orb -m ubuntu` from macOS).
`REF` is an existing v1 artifact JSON reference. Omitting `ARCHIVE` uses its
existing S3 bucket/key/region. When supplied, `ARCHIVE` is the matching regular
external fixture file, outside `CACHE`. Optional manifest files belong
to the caller and must be retained/budgeted with the calling workflow; the cache
does not silently write them somewhere else.

```sh
# Freshly verify every archive member; this does not materialize payload.
python3 workbench/spikes/storage-lifecycle/acquire.py inspect REF ARCHIVE

# Default slim collection: returns checked identities, without unowned paths.
python3 workbench/spikes/storage-lifecycle/acquire.py \
  --root CACHE --budget 1048576 acquire REF ARCHIVE

# Normal scoped consumption. Read JSON from the inherited SIXDB_INPUTS_FD.
# SIXDB_INPUT is the first path for one-input programs. Small maps retain
# SIXDB_INPUTS_JSON only for compatibility; wide maps do not enter the environment.
python3 workbench/spikes/storage-lifecycle/acquire.py \
  --root CACHE --budget 1048576 run REF ARCHIVE \
  --file inputs/column.bin -- python3 CONSUMER.py

# Full recovery, with explicit persistent pin for later independent use.
python3 workbench/spikes/storage-lifecycle/acquire.py \
  --root CACHE --budget 1048576 acquire REF ARCHIVE --full --pin recovery-UNIQUE_TOKEN
python3 workbench/spikes/storage-lifecycle/store.py \
  --root CACHE --budget 1048576 release recovery-UNIQUE_TOKEN

# Warm/offline scoped consumption using a previously checked complete manifest.
python3 workbench/spikes/storage-lifecycle/acquire.py \
  --root CACHE --budget 1048576 run REF ARCHIVE --manifest CHECKED_MANIFEST \
  --file inputs/column.bin -- python3 CONSUMER.py

# A unique accepted result becomes evictable only after explicitly matching it
# against independent archived bytes. This command does not perform the upload.
python3 workbench/spikes/storage-lifecycle/store.py \
  --root CACHE --budget 1048576 put evidence-KEY UNIQUE_RESULT_FILE
python3 workbench/spikes/storage-lifecycle/acquire.py \
  --root CACHE --budget 1048576 archive REF ARCHIVE evidence-KEY result/member.txt
```

`--file` names are relative to the reference's optional `subdirectory`, matching
legacy recovery behavior. `--full` and `--file` are mutually exclusive. `inspect`
returns full archive member names so provenance and full-archive expected hashes
stay unambiguous. The automatic finite stream-work allowance is the maximum of 64 MiB, eight
times the recorded compressed byte count, the managed store capacity (when
present), and any caller-supplied full expanded member-size declaration plus
bounded TAR/PAX/header padding. It supports ordinary inputs larger than the old
fixed 256 MiB ceiling. `--max-expanded` remains an authoritative explicit ceiling,
including when a manifest or budget would suggest more. Unknown very high
expansion ratios still refuse; supply a known full manifest, a suitable managed
capacity or an explicit finite work allowance. All derived/explicit values are
bounded to signed 64-bit byte counts. These are decompression-work limits,
separate from selected-payload admission; a 300 MB archive scan does not require
a 300 MB cache when only a small member is materialized. Archive references allow at most 8,192 regular members and
1,024 UTF-8 bytes per canonical member name. Metadata readers have byte limits.

Only regular files and bounded local PAX extensions emitted by the existing
packer are supported. Links, sparse files, directories, GNU/global extensions,
noncanonical/duplicate names, file/parent conflicts, bad checksums, malformed
PAX records and nonzero trailing tar data refuse the operation. This is the
legacy packer's file subset; it is not a general-purpose `tar` extractor.

Unpinned acquisition returns identities only; `run` is the usable default for a
reader. A persistent `--pin` is deliberately separate and must be released.
Tokens are group labels: use a fresh token for an independent consumer rather
than reusing a descriptive name. `SIXDB_INPUTS_FD` identifies a sealed anonymous
`memfd` containing the UTF-8 JSON mapping; the maximum is 8 MiB. It creates no
on-disk view or permanent metadata. Consumers can read it with
`json.load(os.fdopen(os.dup(int(os.environ["SIXDB_INPUTS_FD"]))))`. For mappings
of at most 32 KiB, `SIXDB_INPUTS_JSON` is also provided for compatibility. Larger
mappings omit that variable and use the descriptor, so ordinary wide member
sets launch successfully rather than encountering `E2BIG`. The descriptor is
sealed against writing/growing/shrinking and closed with the scoped lifetime. Cache aliases/recovery metadata retire only
when the object is independently recoverable, unchanged and unpinned; the cache
is not an eternal job index. Callers retain the small original artifact
references needed to reacquire retired historical content.

An unarchived `put` stays unique even when another fetch finds matching bytes.
Only the explicit archive transition changes that protection. Failed archive
verification preserves the original unique object. Edited local CAS bytes also
refuse normal reads/eviction and are preserved for recovery rather than
silently repaired or deleted.

## Capacity and integration boundaries

The store's admission math and namespace allocation checks are useful early
refusal and tested accounting. Preallocating the whole payload prevents a
streaming producer from exceeding its declared output allowance. They are **not
a portable hard physical-space cap**: filesystem tree/inode metadata and
open-unlinked storage need the enforcing fixed-image workspace being developed
separately. Its kernel capacity must enclose `objects`, staging, journal and
catalog, and its aggregate maximum backing allocation must be charged outside.
A post-write `used()` check is not evidence of a physical peak bound.

The scoped consumer is a lifetime guard, not a filesystem security sandbox.
A same-user program can intentionally chmod/write a shared file, create new
files in an ordinary writable store, or copy input outside it. Such writes
cannot become uncharged merely because the input was acquired correctly.
Mutable workspace/output confinement and redirection of temporary files,
stdout/stderr and compiler output remain the other wrapper's obligation.

Holding one exclusive lock through transfer, verification and consumption is
intentional prototype serialization. The S3 adapter is locally checked with a substituted transport; a coordinated
small real-object validation is pending. Large archive rereads can be expensive. Reuse a verified complete manifest for
warm reads and batch member recovery in one acquisition. Do not hide redundant
transfer cost under microbenchmark timings or add parallel reservations without
evidence that the serial design is inadequate.

Before production adoption, the existing `worker.py` and `artifacts.py` entry
points must delegate acquisition/consumer ownership here, shared across
checkouts. Input dependencies and source packages remain referenced, default
collection must not copy shared inputs back into results, publish/upload and
preparation staging must fit the enforcing pool, and old unique/pinned/edited
material needs owner-coordinated migration. None of those obligations is proved
by these local fixture commands.

## Author checks and independent qualification

The frozen starting store was
`216733a39d340b3b433a7de4413ab824e0e8294b6269762061cfcb3824b0977d`.
The author's tiny local fixture checks and command transcripts are retained under
`build/reviews/storage-20261024/adversarial-growth/integration-own/`. They exercise
ordinary repeated archives, selected overlapping members, full source recovery,
scoped consumption, unique-to-archive, retirement, contention, malformed
archives, separately wrong member hashes and fresh recovery corruption.

A 1.5 MB omitted source package was validated without materializing it under a
256 KiB namespace budget; asking for full recovery refused before payload
staging. A checked-manifest scoped read succeeded with the archive file absent;
an edit to an unrequested manifest member invalidated that proof and refused.
These are author checks, not independent qualification or a deployment claim.
Fresh external adversarial agents must attack the complete archive path and
fixed-image integration before any stronger conclusion.

The completed author replay froze **both** modules:

- `store.py`: `5df915ee47486391b449661b01945c3afc1183069dd117292be80322992c89bb`.
- `acquire.py`: `ead261a602811b5c7d39063112997180dd78776ffd3631b3c78d4bd4ecc8dc5b`.

Eighty repeated mixed archive collections and eight contending selected
acquisitions passed under a 524,288-byte namespace budget. Independently walked
post-operation allocation peaked at 245,760 bytes; the workload's final cache
was 69,632 bytes. The catalog contained 26 content entries after 80 distinct
result variants, rather than retaining every historical variant. These are
post-operation observations, **not** measurements of every in-flight interval.
The entire isolated author fixture tree was below 5 MiB.

The same frozen pair also passed offline checked-manifest consumption, refusal
after editing an unrequested manifest member, SIGKILL/restart at `after-stage`,
`after-object` and `after-catalog`, pre-existing unique-byte survival, ordinary
regular-file CLI operations and propagation of consumer exit status 7. See
`selfcheck-result.json`/`selfcheck-transcript.json`, `extra-checks-result.json` and
the frozen `candidate/source-sha256.json` beneath the fixture directory.

## Reviewer counterexample and repaired acquisition cut

The fresh acquisition reviewer found a real correctness bug in the first frozen
pair above: the warm proof authenticated member sizes/hashes while default
selection trusted an unauthenticated `rebuildable` flag. Editing only that flag
could cause a genuine ordinary file to disappear from a warm default view. The
old pair remains frozen for reproduction. The repair binds boolean policy flags
into a versioned proof, checks supplied flags against actual member prefixes on
a fresh scan, and refuses an edited proof when the backend is unavailable.

The previous 256 MiB default also rejected a real ordinary input. Read-only
inspection of
[Frame's input reference](../frame-phase-1/encoding/worker-inputs.json) confirms
29 members and 75,091,839 compressed bytes; the review reported 305,807,360
expanded member bytes. The automatic allowance for that reference is now
600,734,712 bytes with a 512 MiB store budget. This is a work allowance, not a
disk reservation. No real input archive was fetched for this repair.

A disposable synthetic archive with **29 ordinary members totaling exactly
305,807,360 expanded bytes** passed default acquisition with adequate capacity.
Its repeated zero content shared two CAS payloads, keeping the independent
fixture peak at 15,937,536 allocated bytes (below 16 MiB). The verified synthetic
CAS was then removed; its tiny compressed archive, generator and results remain.
A known full manifest also allowed a 12-byte selected member under a 256 KiB
disk budget. An explicit 32 MiB work ceiling still refused, as did an unknown
high-ratio scan without a sufficient budget, known manifest or explicit ceiling.

A normal 512-member archive then launched a consumer and read a mapping larger
than 128 KiB from the sealed inherited descriptor, with one shared payload
object. It did not merely demonstrate safe `E2BIG` refusal. Old-cut failure,
new-cut policy refusal with/without the backend, real-reference metadata checks,
expanded-stream and wide-consumer transcripts are retained in
`build/reviews/storage-20261024/adversarial-growth/integration-own/revision2/`.
These remain author regression checks; fresh independent qualification is owed.

The repaired frozen pair is retained in `revision2/candidate/`:

- `store.py`: `4f0169e68785aa1dc5cbef1e7ce8ce269312b5f8585e897acb18dce5f7fa47d1`.
- `acquire.py`: `023bc2f88f0474bc41b9bea3f3d0281ea2157137bc13aeab11394dac1cbd511a`.

The full 80-job/eight-contender replay also passed on this pair, with an
independently walked post-operation maximum of 241,664 bytes and final 73,728
bytes under the 524,288-byte namespace budget. The same pair passed offline
checked-manifest reading, the three publication SIGKILL/restart points, unique
survival, ordinary raw-file CLI and consumer exit-code propagation. This is
regression evidence for the repaired cut, not independent qualification.

## Reopenable source API and bounded recent proofs

The fresh composed review ([V5](adversarial/COMPOSED-WORKFLOW-V5.md)) found that
job B overwrote a shared object's only proof of archive A. The repaired catalog
separates recent membership hints from destructive-eviction recovery. The author
replayed A online, A offline, B online and A offline again with the same bytes,
and retained a compact result showing success. Eighty extra archive variants
kept the hint map within its byte/count caps; a recent A/B pair still worked.

Read-only inspection of the compact `operations/workers.json` found 308 v1
references, five above 4,096 files and a maximum of 4,918. The finite 8,192 limit
accepts all existing metadata. A real synthetic 4,918-member archive and ordinary
scoped consumer completed with one shared payload object; no historical worker
archive was fetched to establish this compatibility result.

Public callers use:

```python
from acquire import acquire, inspect_archive, LocalArchiveSource, S3ArchiveSource
from store import Store

store = Store(cache_root, byte_budget)
try:
    # None defaults to S3 from the unchanged v1 reference.
    result = acquire(store, reference, source=None, selected=names,
                     expected=checked_manifest, consumer=True)
    # Explicit source objects also work; a Path is a local-source shorthand.
    source = S3ArchiveSource(reference, seconds=300)
    manifest = inspect_archive(reference, source, store=store)
    # The caller owns the consumer mapping and its actual mutable work.
finally:
    store.close()
```

`source` is reopenable. S3 uses the same `aws s3 cp --only-show-errors URI -
--region REGION` command as `artifacts.remote_manifest`, with stdout streamed
through compressed/member verification. It writes no compressed download or
stderr file. Stderr is drained to at most 64 KiB anonymous memory (only the last
4 KiB is included in an error), with overflow refusal; the stream has a finite
1–3,600 second deadline (300 by default). `TMPDIR` points inside managed staging
when a Store is supplied. Complete bytes followed by a nonzero CLI exit still
refuse before publication. Parser errors, timeout, interrupted process and
stderr overflow terminate/reap the transport.

Standalone transport gets a parent-death guard and private PID namespace. A
trusted enclosing volume/flow supervisor may construct `Store(...,
outer_guard=True)` to supply the enclosing descendant lifetime instead; this is
an explicit Python composition argument, never an environment or persisted
recovery bypass. Stable mounted production volumes can use the ordinary False
default, which creates the source's own guard. These process controls do not
replace the enforcing filesystem/outer reserve.

Fake-transport author checks exercised streaming, two source opens without a
manifest, one with a complete declaration, valid bytes followed by exit 17,
corruption, stderr overflow, finite timeout and manager SIGKILL. Previously
accepted unique bytes survived all failure paths. Fresh corrupted S3 recovery
refused eviction while preserving local bytes; valid independent verification
allowed eviction, and ordinary `store.get` then recovered the accepted member.
Fixtures/results are retained in `integration-own/revision3/`. No actual S3
request occurred during those local checks; the one small operational-object
probe is coordinated with the integrating task to avoid duplicate reads.

The local integration handback froze the following source pair together in
`integration-own/revision3/candidate/`:

- `store.py`: `00ed707b310edd4b97db1017cbfc15ed54f8c358118961cf46fb884059a5357e`.
- `acquire.py`: `0570f62192349777f085f5018fa65d48ccc624ee4e323f8c3ff48478ac94652c`.

The frozen replay repeated all new hint/count/source regressions. Separate
transport-count controls observed two opens for a cold acquisition without a
manifest, one for a cold complete declaration, and zero for a resident warm
read. An accepted unique result also completed the explicit S3-backed archive
transition using the fake transport. Actual public-command qualification and
the coordinated small real S3 probe remain with the integrating owner. These
files are ready to move into the canonical tools package with relative imports;
research CLI wrappers should delegate there, not remain a parallel manager.
