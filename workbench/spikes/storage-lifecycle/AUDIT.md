# Storage lifecycle audit

October 24, 2026. Independent read-only audit of the working-tree tools, including
provisional source-archive omission and `artifacts.py input`. No production
files, real cache contents, S3 objects or worker state were changed. Two tiny
isolated probes below used temporary files and mocked free-space observations;
they did not allocate compute or contact a remote service.

## The failure is admission, not a missing sweep

The 10 GiB worker and 60 GiB local-build values are targets used by explicit
maintenance. They are not conditions for accepting new retained bytes. A healthy
`worker_cache.preflight()` calls only `storage.require_space()`. That function
checks current filesystem free space against a fixed reserve; it does not read
either budget or reserve capacity for another writer. Its comment explicitly
says healthy operations bypass cache scans. The first successful operation can
leave all its bytes locally and the next operation can do the same, until the
device approaches the reserve. Replacing a timer or updating an accounting report
cannot change this acceptance rule.

The observed 138 copies of Frame inputs are one instance of this failure, not
its complete definition. Removing those copies fixes today's multiplicative
producer. A different corpus, source variant, new capture, failed download or
large log can still consume the remaining space through a permitted path.

## Actual paths and lifetimes

| Producer / consumer | Existing behavior | Missing bound or ownership transition |
| --- | --- | --- |
| `worker.py:snapshot`, `prepare`, `dispatch` | Writes a complete compressed source in each job directory, persists source metadata, then uploads job files. | A free-space estimate precedes source packing, but no worker budget reservation. A job that fails before a final result archive can leave its launch package outside ordinary closed-result eviction. |
| `worker_cache.collect` + `artifacts.restore_bundle` | Serializes collection per job; a shared transfer lock serializes the download. Stages a complete compressed bundle and selected expanded output, then publishes a results directory and member receipt. | Neither admission nor publication enforces the worker budget. Other producers do not share the transfer lock. A SIGKILL can leave `.collection-*` / `.fetch-*` staging. Inventory counts it but ordinary archived-member eviction cannot remove it. |
| `worker_cache.reclaim` | Re-reads a full remote archive, hashes members and preserves edited files/pins; writes recovery metadata before deletion. | This is a useful explicit migration/repair operation, not an ordinary bounded lifecycle. Recent access, pins and unarchived data can keep the cache arbitrarily over budget. |
| `artifacts.publish` | Packs all retained output, uploads, then downloads a second complete compressed copy for verification. Generic worker results can include anything a script put in `SIXDB_RESULTS`. | Peak local demand includes original output plus **two** compressed copies. No budget admission or packing/download bound exists in this path. Repeated interrupted attempts can leave distinct transaction directories. |
| `artifacts.restore_inputs` | Restores a shared prepared dataset, then `shutil.copytree`s it into each recovered run. | Input references prevent duplication in S3, but ordinary full recovery still creates independent local copies. The copy checks free space; it does not consume a dataset/build budget reservation. |
| `datasets.source` | Hash-checks a pinned source download and keeps it by checksum. | S3/HTTP downloads are not checked against the storage budgets; source byte count is checked only after transfer. Variants remain indefinitely. |
| `datasets.cached` / `restore` | Atomic per-key preparation/restoration, checked content identity, shared cache when configured consistently. | Recipes can write arbitrary output before any declared allowance. New parameter/recipe variants create more permanent trees. No variant budget, eviction eligibility or active-reader lease exists. |
| Provisional `artifacts.cached_input` | One atomically filled, verified tree per full reference and exact selection; uses `SIXDB_DATA_CACHE`, excludes `SIXDB_RESULTS`. | Fixes repeated identical calls but has no admission/eviction budget. Overlapping selections and different references to shared bytes make separate trees. Per-key locks do not prevent concurrent different keys from spending the same space. |
| `capture.py:capture` | New Git worktree, independently owned source files, copy-on-write where supported, adjacent identity receipt. | No budget admission or hard write limit. Divergent captures and worktree metadata accumulate. Reflinks reduce physical duplication, not the count/lifetime of captures. |
| `experiment.Run` | Reuses an explicitly named build workspace; packs a source archive for each run. `Run.input` records a shared dataset rather than copying it. | Source packing, captured workspace growth, stdout/stderr and child build output do not pass a storage admission API. The workspace lock coordinates reuse, not capacity. |
| `local_cache.inventory` / `maintain` | Scans conventional build roots across registered worktrees, counts pinned/unique output, removes only recognized idle CMake objects. | A retrospective scanner cannot cap recent builds, new outputs or non-CMake products. Seven-day idle policy, a running compiler, pins and unique evidence can correctly prevent every deletion while new output continues. |

Code locations: `storage.py:36`, `worker_cache.py:434`, `worker.py:279`,
`artifacts.py:174,207,471,529`, `datasets.py:66,98,179`, `capture.py:58`,
`experiment.py:113,168,193`, `local_cache.py:55,190`. Line numbers identify the
inspected working-tree cut; use the function names after edits.

## Two bounded counterexamples

An inline probe imported the actual helpers with a temporary directory and
mocked `storage.headroom()` to report 100 GiB free. It set
`SIXDB_WORKER_CACHE_GIB=0`, called `worker_cache.preflight()` and then requested
1 MiB via `storage.require_space()`. Both accepted; `maintain` was replaced with
an assertion to prove no pruning path ran. This is the old acceptance rule even
with a zero worker budget. It is not a simulated filesystem-exhaustion test.

A second probe set `SIXDB_LOCAL_BUILD_GIB` to exactly 16,384 bytes and pointed
`SIXDB_DATA_CACHE` at an isolated temporary cache. Nine calls to the real
`datasets.cached()` with nine parameter values each created an 8,192-byte
column. All nine were accepted: **73,728 payload bytes / 110,592 allocated bytes**
against the 16,384-byte configured local budget. The temporary tree was removed
by the fixture's context manager. Changing the source of the inputs does not
alter this missing admission behavior.

These are failure reproductions, not coverage of crashes or adversarial paths.
Other adversarial agents own those experiments.

## A small replacement boundary

Replace the managed immutable acquisition lifecycle first. Do not make another
whole-tree scanner the control plane. Existing immutable S3 references and member
hashes remain recovery truth; no replacement archive format is required.

One shared managed store should own fetched source, prepared artifact input and
worker result bytes. It should expose four operations rather than several
independent caches with incompatible budget rules:

1. **Acquire known bytes** by content identity / immutable recovery reference,
   returning a reader lease and usable path. Repeated jobs, overlapping member
   selections and different manifests referring to the same content should share
   the physical stored payload rather than manufacture another results tree.
2. **Admit a bounded write** with an allowance for new payload, temporary bytes
   and metadata. Reserve under one interprocess coordinator **before** writing.
   A streaming writer cannot exceed the allowance. Compressed staging is either
   absent or separately charged; inspecting a small selected member must not
   silently demand another complete unbudgeted archive.
3. **Commit checked content** only after expected size/hash and required durable
   publication succeed. A returned path must not refer to partial or unverified
   bytes. Store lookup metadata and reference handles are charged too.
4. **Release or pin a lease** and evict only verified remote-backed unleased
   content. Active or pinned bytes remain counted; full capacity yields a safe
   refusal, not deletion of the material that explains the pressure.

An exclusive interprocess operation lock is an acceptable first small prototype
if concurrency is not useful yet. It is easier to test than a concurrent
reservation database with unproven recovery. Holding it through a slow network
operation has an explicit usability cost; measure that cost before adding
parallel machinery. Persistent lease/reservation records need process identity
and restart handling; a stale PID alone is not sufficient identity.

The coordinator must reconcile its own actual namespace on restart, including
staging, metadata, directory allocation and unknown files. Transaction-owned
incomplete downloads may be discarded after proving no writer owns them; an
unrecognized file is not automatically junk. Unknown or damaged state must be
counted and preserved/refused, rather than excluded to satisfy a number.

Keep writable researcher material outside immutable cache semantics. A changed
file is a new authored/unique object until explicitly retained or relinquished;
it must not become an evictable byte because its old filename had an archive
mapping. Returning writable hardlinks to shared cache payload is unsafe: an
edit changes every borrower. Reflink/materialization output needs its own
admission and lifetime; it cannot be called free merely because initial extents
were shared.

## Lease and recovery details that matter to the prototype

A CLI `get --lease OWNER` exits before the later reader starts. Its own PID
cannot represent that reader's lifetime. An explicit persistent lease may be a
useful prototype pin, but forgetting to release it eventually produces safe
refusal and more operator attention. Production scoped use needs an acquire
context or command wrapper that holds protection through the consumer's actual
lifetime. Killing the wrapper while its child survives is a relevant failure
case. Keep explicit durable pins separate from temporary reader leases.

Mint distinct lease tokens; owner labels describe the work rather than granting
permission to release every reader with the same label. Recovery after a crash
must not mistake a recycled PID for the original process.

A recovery path inside the managed root cannot prove independent recovery: the
eviction candidate could certify itself and then be deleted. Reject that scope.
The local-file backend used by an isolated prototype is an immutable-backend
model, not an S3 durability proof. At minimum, changing or corrupting that file
after initial import must block eviction while preserving accepted bytes.

## Mutable builds need a different honest statement

A storage API cannot hard-bound `clang`, Cargo, CMake, a Python preparation
callback, shell redirection or a user's editor when those writers target an
ordinary directory directly. A reserve checked before launching a compiler and
a periodic/post-run `du` check are both soft limits. They can detect overshoot,
but cannot prevent a single child from filling the filesystem first.

For truly bounded mutable build work, choose an enforceable filesystem boundary
with a quota or capacity-limited workspace and run the producer inside it.
Subvolume/project quota or a deliberately bounded filesystem are candidates to
probe, not claims about this host's configured support. Avoid enabling a global
filesystem feature or formatting a real device as an incidental validation step.
The build allowance must include active/pinned work; when it is full, refuse new
work or choose a separately authorized capacity instead of stripping useful
incremental state.

If the first prototype controls immutable acquisitions only, say so explicitly.
It has not replaced the entire local-build problem. Legacy ignored trees also
need a one-time owner-coordinated migration; they do not disappear from the
capacity model because the new store has a clean root. Host disk space still
requires a safety reserve because other applications and APFS/OrbStack backing
allocation remain outside the tool's control.

## What can be removed or narrowed

- Retire age-based sweeping as the mechanism that is supposed to enforce the
  cache budget. Keep exact old-archive eviction only for migration and explicit
  recovery/curation, with its current protection of edits and unique evidence.
- Remove ordinary per-job launch/result source duplication and automatic input
  rematerialization. A job's small source/input reference can resolve shared
  bytes when needed. Local job-reference metadata itself must have a retention
  policy; a million tiny receipts are not free.
- Consolidate independent dataset/artifact/member cache acquisition and locking
  behind one bounded owner. Keep recipe identity and benchmark semantics with
  their existing callers; they do not need a universal experiment adapter.
- Replace full-output-copy repair with publication of checked immutable payload
  plus a small view/reference update. Keep conflicting authored files out of the
  cache namespace so repair does not manufacture unbounded `replaced-results-*`
  trees on every retry.
- Keep `local_cache` as a truthful legacy diagnostic until mutable writers have
  enforceable workspaces. Its report must not imply a cap or a fresh guarantee.
- Do not restore a timer. The point of a managed capacity boundary is that
  admission/release/restart enforce the invariant during actual work.

## Adversarial acceptance questions

The external test oracle should account for **actual allocated bytes** under the
managed root and independently inspect every surviving/recovered byte. Include
metadata, orphan staging and live reservations. `st_blocks` can conservatively
overcount shared reflink ranges, so either avoid that ambiguity in the tiny
fixture or separately measure the enforcing filesystem's charged capacity.
A sum of sizes from a self-reported manifest is not a physical-space proof.

First reproduce the old repeated-job and capacity-bypass behavior. Then run the
same workload through the candidate and attack different-key concurrency,
kill/restart at every state transition, interrupted verification, actual small
filesystem ENOSPC, corrupt references/data/receipts, symlink/parent swaps,
reader-vs-evict and pin-vs-admit races, stale process identity, many tiny files,
large compression expansion and overlapping selections. Include a deliberately
external writer to demonstrate the claimed boundary rather than silently
assuming it away. Every accepted operation must satisfy the bound and return
checked bytes; every refused or interrupted operation must preserve existing
accepted/unique work and leave a recoverable state.

Status: audit complete; replacement and adversarial qualification remain open.
