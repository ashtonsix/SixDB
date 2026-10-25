# Independent prototype review

Reviewed the initial `store.py` candidate, the descriptor-relative revision
`ec82b496658ca62b328139f09167a15adb4827d9fad97a441cb16d302dea0f16`,
and the FIFO repair `617d1b7fd6fc0cfc2e6ceac75aee2641c651addbd68effab172bc9755f7c24ba`.
This agent authored the audit/review and the separately reported bounded-image
fixture, with no production edits. Kernel tests used tiny isolated
`TemporaryDirectory` fixtures; no real cache, S3, builds or workers were touched.
The candidate is changing during the spike: results below identify their cut,
not a blanket qualification of subsequent source.

## Findings

### Directory check/use race — reproduced, then corrected

The initial candidate called `check_layout()` and subsequently performed
`stage.replace(target)` or `path.unlink()` through a pathname. A parent could be
replaced between the check and the filesystem mutation.

A deterministic fault-injection probe wrapped `check_layout` so it first ran the
real successful check. Only when called directly from `ingest`, the wrapper then
renamed `objects/` aside and replaced it with a symlink to a second private
fixture directory. That directory contained an unrelated sentinel named by the
incoming object's SHA-256. This models the rename in the actual check/use gap;
it does not patch the copy, rename, hashing or error-handling implementation.

Initial result: the outside sentinel was overwritten with the incoming bytes.
The subsequent directory sync raised `NotADirectoryError`, after damage had
already occurred. Merely returning an error was not safe failure.

The reviewed revision uses held directory descriptors and `dir_fd`-relative
open/replace/unlink operations. Running the exact same probe against
`ec82b496…` refused the operation and preserved the outside sentinel. This
counterexample is corrected in that cut. Keep it as a regression; another
pathname precheck would not be an equivalent repair.

A returned ordinary pathname still assumes the managed namespace remains
private through consumption. Descriptor-relative publication does not grant
protection from arbitrary external deletion/rename by the same OS user.
State that boundary rather than promising a security boundary the OS has not
provided.

### FIFO source/recovery can hang the global operation lock — reproduced, then corrected

`digest_file` opens with `O_RDONLY | O_NOFOLLOW` and only afterwards performs
`fstat` to reject non-regular files. Opening a FIFO this way waits for a writer
before the regular-file check can run.

A subprocess `put` against a private FIFO failed to finish within a one-second
fixture timeout and was killed by the test harness. No bytes were written to the
FIFO. The same issue applies when a previously registered external recovery path
has become a FIFO: eviction can block while holding the whole store lock.

Use a nonblocking descriptor open and then verify its regular-file type, or an
equally safe descriptor-based open strategy. Checking type by pathname before
opening would introduce another race. The subsequent `617d1b7f…` revision uses a
nonblocking open. Repeating the same one-second subprocess fixture returned
status 2 immediately with `only regular files are supported`; it did not time
out. This counterexample is corrected in that cut.

### The physical in-flight bound is not established

The candidate reserves `ceil(size/4096)*4096`, serialized catalog byte estimates
and a fixed 64 KiB margin. It checks actual `used()` only **after** writing and
fsyncing payload or metadata. On an underestimated allocation, the operation has
already exceeded the proposed cap before returning refusal. A post-write error
cannot prove an in-flight maximum.

`used()` sums directory/file `st_blocks`. This is a useful namespace allocation
observation, but it does not include all filesystem metadata, such as Btrfs tree,
inode and checksum blocks. Nor does the walk see an inode whose directory entry
was removed while a reader still holds it open. A quota or bounded filesystem
continues to charge such storage even after `du`-style accounting drops it.

The margin can be tested for the exact tiny fixture filesystem; it is not a
portable physical-space invariant. State the candidate's measured accounting
bound separately from a filesystem-enforced capacity boundary. If hard physical
containment is required, test on an enforceable quota/capacity-limited workspace.
That also makes real ENOSPC experiments meaningful without threatening the host.

### Lease labels currently behave as persistent pins

The CLI exits before its consumer begins reading the returned path. Therefore
its own PID cannot represent that later reader. `get --lease OWNER` persists a
label until `release OWNER`; forgetting release eventually causes correct safe
refusal but recreates operator cleanup work. Reusing an owner label conflates
independent readers, and releasing it releases every digest under that label.

The prototype can describe these as explicit persistent pins. Production scoped
consumption needs a distinct lease token and a context/wrapper that retains it
through the consumer's real lifetime. Killing a wrapper while its child keeps
running is an important counterexample. A reused PID or expired wall clock is
not proof that the original reader ended.

`get` without a lease returns a path that another admission can evict before the
caller opens it. An unleased reader may also retain an opened, unlinked inode,
which defeats path-walk allocation accounting. Avoid making correct lifetime
management an optional footnote in the normal interface.

The reviewed revision changes copied object mode from writable `0600` to
read-only `0444`, improving accidental-write protection. It is still the same OS
owner, not privilege isolation. Writable research material needs a separate
bounded private materialization; it must not edit shared payload in place.

### Crash sequencing is conservative, with limits to the evidence

For ordinary process kill, the pending digest identifies the object that may
have been renamed before catalog publication. Restart discards it only if it is
absent from the durable catalog and its bytes still hash to that digest. An
unknown object with a lost/missing pending record is preserved and refused,
rather than guessed disposable. Existing cataloged unique bytes are not eviction
candidates. These are useful small-state choices.

A corrupted/missing catalog or journal can still require operator investigation;
there is no second authoritative local metadata copy. This is preferable to
silently resetting ownership and deleting unique data, but is not automatic
recovery from arbitrary metadata loss. A mutable fixture recovery file also
models only an immutable backend: source changes before eviction must be
rejected, but this alone does not establish S3 durability or protection from
later backend deletion.

Inspect cleanup durability as well as publication: `reconcile` deletes the
pending journal before syncing the object/staging directory removals. Process
kill does not reproduce a power loss that durably removes the journal but loses
an earlier unlink in another directory. That outcome should at worst leave an
unknown preserved object and refusal, not unique-data loss, but it can prevent
automatic restart. An fsync-order or power-failure test is needed before claiming
that stronger recovery behavior.

## Production replacement without another parallel framework

Keep the useful current user interfaces and archive format. The new lifecycle
should become the implementation of `worker.py run/wait/fetch/evict/cache`,
`artifacts.py fetch/input`, dataset acquisition and capture/build materialization;
`store.py` should not become an additional permanent command family users must
learn while the old paths remain unrestricted.

The raw-byte prototype does not yet supply collected directory views, remote
member streaming, S3 recovery, source preparation or bounded mutable build
execution. Those are integration obligations, not details already proved by a
successful `fetch KEY FILE` test.

A concrete progression is:

1. Put source/input/result acquisition behind one shared manager across
   controller worktrees, with one admission and recovery policy. Keep recipe
   identity and benchmark operation semantics in existing callers.
2. Preserve old-job S3 references and member names. Ordinary collection should
   publish a small checked result view whose data resolves shared immutable
   payload. Selected recovery should acquire only required bytes. A directory
   view/materialization needs a counted lifetime; copying it outside the store
   cannot be treated as free. The present kernel refuses links and has no such
   view mechanism yet.
3. Route packing, verification downloads, preparation and repair staging through
   the same bounded transaction owner. Eliminate the two-compressed-copy publish
   peak or explicitly reserve it. A legacy `shutil.copytree` must not remain as
   an unbudgeted escape path after acquisition has become bounded.
4. Migrate existing local files with owner/recovery checks, preserving changed,
   pinned and unarchived material. Keep the legacy eviction routine only for
   this migration/explicit curation, then remove its role as normal enforcement.
5. Give mutable child builds an enforceable workspace capacity boundary or keep
   the limitation explicit. Merely checking before/after `cmake --build`, Cargo,
   a Python recipe or shell redirection cannot prevent a single child filling
   the device. A clean new store does not erase unbounded legacy build roots.
6. Run the repeated-job/adversarial workload through those **real existing entry
   points**, not only the isolated kernel. Include variant inputs, failed jobs,
   old archives, exact recovery and cross-worktree concurrency. Verify actual
   retained and temporary allocation plus accepted-byte survival independently.

Until these interfaces actually use the manager, passing kernel tests is useful
spike evidence and the replacement remains open. Do not call the original disk
problem solved or leave two supposedly authoritative storage systems running.
