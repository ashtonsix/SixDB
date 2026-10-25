# Independent reader and metadata review, 2026-10-24/25

The original frozen implementation had three consequential failures: a live
reader could retain unlinked allocated bytes after wrapper failure; a successful
verified fetch did not replace a stale recovery reference; and metadata trimming
could delete a later requested alias during a valid multi-input `use`.
Normal command exit also allowed a forked reader to retain unlinked bytes, and
the wrapper initially returned success for a failing command.

All ten probes completed against the revised frozen cut
`216733a39d340b3b433a7de4413ab824e0e8294b6269762061cfcb3824b0977d`
without reproducing those failures. This is bounded evidence for the regular-file
fixture backend, not a production integration result or a proof for arbitrary
storage and process behavior.

## Independent starting point and method

This review initially read only `AGENTS.md`, the current `store.py` source and
its CLI help. It did not read earlier reviews or assume their results. The
first frozen source, copied before any fixture ran, was:

```text
f8cbff0022b9eeed47d7731163cc13a0043aab5864551337f857743db31a1ee2
```

The source/help described recoverable aliases as disposable hints, unique bytes
as protected until explicitly archived, `--lease` as a persistent pin, and
`use KEY [KEY...] -- COMMAND` as ordinary scoped consumption. The tests use those
stated distinctions rather than treating every historical alias as permanent.

The owned harness is [reader_metadata_v2.py](reader_metadata_v2.py). Every fixture
has a separate temporary directory under ignored
`build/reviews/storage-20261024/reader-metadata-v2/`, a 262,144-byte managed budget,
and less than 16 MiB of allocated fixture material. Sources are deterministic,
independent regular files outside the managed root. No actual cache, cloud,
network, benchmark build, tool installation or production implementation was
touched.

Allocation checks sum actual `st_blocks * 512` for directories and files,
deduplicated by inode. They do not consume `stats`. The harness checks after
operations and samples visible allocations at approximately 1 ms intervals;
these samples are observed high-water values, not a proof of every transient
allocation. The reader failures additionally inspect the live consumer's
`/proc/HOST_PID/fd/FD` and charge the unlinked inode, which a pathname walk
cannot find. Remaining managed objects are compared byte-for-byte with the
independent expected inputs. Accepted unique paths and reader contents are
checked separately. Every CLI operation starts a new process, exercising
catalog reopening/reconciliation throughout churn.

Revised scoped consumers run in a PID namespace, so the harness resolves the
reader's host PID through its own wrapper subprocess tree and `NSpid`. It does
not mistake the namespace-local PID for the host PID.

## Executable original counterexamples

With the original frozen file retained in the ignored report directory:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/reader_metadata_v2.py --case closed-descriptor-reader
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/reader_metadata_v2.py --case replacement-recovery
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/reader_metadata_v2.py --case multi-alias-retirement
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/reader_metadata_v2.py --case descendant-exit
```

The harness reports observations as JSON. A historical counterexample is marked
with a `finding` field; a zero harness exit status means the probe itself
completed, not that the implementation satisfies its intended contract.

### A reader survives wrapper failure and defeats physical allocation accounting

1. Fetch a recoverable 112 KiB input under a 256 KiB budget.
2. Start `use input -- python3 ...`. The direct reader closes inherited
   descriptors with `os.closerange(3, 256)`, opens its input, hashes it and waits.
   This models ordinary consumer descriptor cleanup; it does not mutate store
   material or acquire the operation gate itself.
3. Kill only the wrapper with `SIGKILL`, leaving the direct reader alive.
4. Fetch an independent 160 KiB pressure input.

The pressure fetch succeeds and the live reader's pathname is unlinked.
Independent allocation is 180,224 visible managed bytes plus 114,688 bytes
retained by the consumer's deleted inode: **294,912 allocated bytes against the
262,144-byte budget**. `/proc/.../fd/...` ends in ` (deleted)`. The reader seeks
and hashes the original bytes again successfully before exiting.

The original inherited-lock technique therefore depended on arbitrary commands
preserving an otherwise undocumented descriptor. Counting only visible files
missed the disk allocation still owned by the consumer.

The `descendant-exit` variant forks a reader which closes those descriptors and
retains its input. The direct command and wrapper exit normally with status 0;
pressure then produces the same 294,912-byte total while the background reader
still has the original bytes.

### A fresh verified fetch retains an unusable older recovery reference

Fetch 112 KiB from `old-recovery`, copy those bytes to an independent
`replacement-recovery`, remove `old-recovery`, and successfully fetch the same
key/digest from the replacement. The original catalog still names the removed
file. Fetching a different 112 KiB input refuses capacity because eviction
rechecks that stale path, even though the just-acknowledged source exists and
matches the digest. The failure preserves bytes, but prevents progress with a
valid replacement recovery reference.

### Multi-input preparation retires its own later alias

Fetch 54 long aliases for the same 1 KiB recoverable input, stopping when catalog
size first crosses the 16 KiB metadata-retirement threshold. Both the oldest
and newest aliases exist. Execute `use newest oldest -- python3 -c pass`.

The original first `get(newest)` trims metadata while protecting only that key
label. The oldest label is retired, although its object digest is protected as
a consumer object. The second `get(oldest)` fails with `KeyError` before the
consumer starts. Alias count falls from 54 to 26. Protecting all requested
digests alone does not protect the key lookup phase of a multi-input operation.

### Child failure was visible only inside JSON

`use input -- python3 -c 'raise SystemExit(7)'` originally returned wrapper status
0 while reporting `{"returncode": 7, ...}`. The revised cut propagates status 7.

## Revised-cut results

The intermediate cut
`c9d126d7cb3b0782491ffc05aeafd50af1b4a5f67c56d2a730c451932ce39baa`
fixed scoped lifetime, refreshed recovery and exit propagation, but still
reproduced the multi-alias preparation failure. The final frozen cut above also
protects every requested key before preparing the first input.

| Probe on final frozen cut | Independently observed outcome |
| --- | --- |
| Changing recoverable inputs and overlapping aliases | 220 changing inputs completed; 198 historical aliases retired; accepted unique bytes remained identical. Final metadata was 12,172 bytes with 29 keys; sampled visible peak was 192,512 bytes. |
| Persistent pin through churn/retirement | Pin object and key survived every churn/restart operation and metadata retirement; explicit release succeeded. |
| Unique protection and archive | Capacity pressure and an incorrect archive refused while unique bytes remained intact. Verified archive enabled eviction and later recovery. |
| Explicit pin versus release | A 112 KiB persistent pin blocked incompatible pressure across separate CLI processes; release restored progress. |
| Ordinary two-input scoped use | Concurrent pressure waited while the reader held both inputs; both final hashes matched; no persistent lease was created. |
| Wrapper `SIGKILL`, ordinary reader | Reader terminated after wrapper failure; subsequent pressure progressed without a stale lease. |
| Wrapper `SIGKILL`, reader closes inherited FDs | Actual `os.closerange(3, 256)` reader terminated before pressure; post-pressure visible allocation was 180,224 bytes, sampled peak 188,416 bytes. |
| Forked reader and normal command exit | Reader terminated when its direct command exited; pressure progressed with 180,224 visible allocated bytes. |
| New verified recovery source | Catalog refreshed to the replacement source and pressure progressed. |
| Child exit and metadata preparation boundaries | Child status 7 propagated; both requested aliases survived threshold trimming and the multi-input consumer ran. |

The final revision uses private user/mount/PID namespaces. These positive scope
checks actually ran on this host; they were not replaced by an unsupported-host
refusal. The observed safety response to wrapper failure is termination of its
consumer, rather than survival of that consumer. Ordinary scoped use leaves no
persistent lease; explicitly created pins still require release.

Run against the retained final frozen cut:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/reader_metadata_v2.py \
  --frozen build/reviews/storage-20261024/reader-metadata-v2/store.216733a3.frozen.py \
  --sha256 216733a39d340b3b433a7de4413ab824e0e8294b6269762061cfcb3824b0977d
```

The completed ten-case full report is
`results-216733a3-all.json`; the descendant probe was also run separately in
`results-216733a3-descendant-exit.json`. Original reports and frozen sources
remain beside them, including `results-all.json`,
`results-multi-alias-retirement.json`, `results-f8cbff00-descendant-exit.json`
and `results-c9d126d7-all.json`. No historical failing report was overwritten
by a revised-source report.

No additional failure was observed in these bounded final-cut probes. Namespace
escape attempts, arbitrary outside writers, filesystem crash durability,
production recovery backends, and a whole-machine capacity controller are not
established by this review.
