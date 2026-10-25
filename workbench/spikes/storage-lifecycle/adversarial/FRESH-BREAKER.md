# Fresh external CLI reliability review

2026-10-24. No reproducible defect found in this independent round. All ten
case groups in [fresh_breaker.py](fresh_breaker.py) passed. This is bounded
evidence for the immutable-store prototype, not a conclusion about the whole
research workflow replacement.

The reviewer read the project instructions, research map, spike catalog,
storage-lifecycle starting evidence, research retrospective, CLI help and source.
The retrospective's useful shared-input identity and the guide's unenforced
retention budgets motivated separate deduplication, admission and preservation
oracles. No earlier reviewer report or test conclusion was read or reused.

## Reproduction and source identity

From the macOS checkout root:

```sh
orb -m ubuntu bash -lc 'cd /home/ashtonsix/sixdb && python3 workbench/spikes/storage-lifecycle/adversarial/fresh_breaker.py'
```

The script copies `store.py` to
`build/reviews/storage-20261024/fresh-breaker/store-frozen.py` before launching
any cases. Every store interaction is a separate CLI process; the harness
does not import or patch the store. The live source before/after the final run
and the frozen file before/after have SHA-256:

```text
617d1b7fd6fc0cfc2e6ceac75aee2641c651addbd68effab172bc9755f7c24ba
```

The ignored `build/reviews/storage-20261024/fresh-breaker/results.json` records
per-case details and those hashes. Running the script replaces only its own
fixture subdirectories and frozen copy there. No production source was edited.

## Observations

| Case | Executed oracle and observed result |
| --- | --- |
| Shared content | Forty `fetch` commands for forty keys retained one 16 KiB object. Final allocated bytes 32,768 against 262,144 budget; first key still returned its expected bytes. |
| Simultaneous admission | Twenty-four concurrent 12 KiB unique `put` processes against 196,608 budget: eight acknowledged, sixteen capacity refusals. Every acknowledged input survived and remained retrievable. Final allocation 114,688; sampled in-flight high water 131,072. |
| Publication crashes | `SIGKILL` at each advertised `after-stage`, `after-object`, and `after-catalog` stop. Pre-restart allocation was 61,440 in each case against 262,144 budget. A previously acknowledged 8 KiB anchor and its reader lease survived all restarts; retry succeeded. |
| Pin and corrupt backend | A leased 64 KiB object prevented capacity eviction. After release, overwriting its isolated recovery fixture still caused safe refusal while local bytes survived. Restoring the correct fixture enabled eviction and subsequent verified recovery. |
| Data writer failure | A child `RLIMIT_FSIZE` of 4,096 caused `EFBIG` during a 16 KiB ingestion. Previously accepted bytes and lease survived restart; retry without the limit succeeded. |
| Metadata pressure/failure | Four separate 5,002-byte lease labels were accepted through `get`; the next was refused under the 128 KiB budget. A 4 KiB file-size limit caused `EFBIG` during catalog publication. Restart retained every acknowledged lease and anchor byte; all leases could subsequently be released and the key retrieved. |
| Changed local fixture | An acknowledged recoverable object's local fixture was changed in place. Capacity pressure and direct retrieval refused while preserving those changed bytes. Restoring the known content allowed eviction/recovery. |
| Damaged journal | Truncated JSON and a traversal digest in `pending.json` were each refused. Accepted object and external sentinel bytes survived. Removing the deliberately damaged journal restored ordinary retrieval. |
| Unsafe links | Symlinks substituted separately for `objects`, `staging`, `catalog.json`, `pending.json`, and `.lock` were refused. The target sentinel was unchanged; restoring the original fixture path allowed retrieval of accepted data. |
| Real ENOSPC | A private 4 MiB tmpfs in a child user/mount namespace was filled to leave 0, 1, 2, 9, or 10 free 4 KiB blocks before a 32 KiB `put`. The first four produced real `ENOSPC`: respectively recognizable catalog scratch, staging scratch, staging scratch, and a published unacknowledged object plus catalog scratch. With ten blocks the operation succeeded. Removing the bounded capacity fixture and restarting retained the accepted anchor/lease and permitted retry in every case. |

Expected byte arrays are retained independently of store metadata. Success
requires a zero CLI exit code and parseable response; successful responses are
checked against those expected bytes. The allocation oracle traverses the
isolated managed root and sums filesystem `st_blocks * 512`, including temporary
files and directories. Concurrent high water is sampled, not an exhaustive
observation of every instant. Crash stops provide exact pre-restart snapshots.

## Limits

This round uses tiny regular-file recovery fixtures, one filesystem, cooperative
store processes and explicit lease tokens. It does not prove power-loss behavior,
arbitrary kernel I/O-error handling, safety against an adversarial writer changing
the managed tree or backend during a verification syscall, or byte bounds for
unmanaged mutable builds/worktrees. `st_blocks` does not measure every filesystem
implementation's internal inode/extent metadata. Corrupt journals are preserved
and require repair; the test does not claim automatic repair from arbitrary
catalog corruption. No cloud, network, benchmark, actual cache or user evidence
was accessed. The only mount was the bounded private tmpfs, destroyed with its
namespace when the test child exited.
