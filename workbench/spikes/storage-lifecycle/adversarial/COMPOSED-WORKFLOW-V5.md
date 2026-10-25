# Fresh composed workflow review, 2026-10-25

Two failures remain in the frozen candidate: a second ordinary job can invalidate
offline reuse of still-present shared input, and the v1 reference parser rejects
five ordinary historical jobs before reading their archives. The tested cache
and image pressure paths preserved accepted bytes and resumed useful work.
This is an independent bounded local review, not a deployment qualification.

## Candidate and starting point

The candidate was copied into private ignored storage before execution. SHA-256:

| File | SHA-256 |
| --- | --- |
| `store.py` | `b0a4c22be48aba7a19bd9bd61d86e30b90392bcc7999cd7a824175753122b771` |
| `acquire.py` | `7ff36f2abf2ef6e84d9fdaf544af89793aa6851ba51852b8519dc49927d32970` |
| `workspace.py` | `fb2cc7522d873bdb90273e639c15212a65ecc2839870c59aeb4d7cc3252b542f` |
| `flow.py` | `122e1b75a5ece57c396d993a9e554108f0b704c0d1c660cd6d5a88af6004cfd6` |

The [storage guide](../../../tools/storage.md) explicitly distinguishes retention
budgets from enforced capacity and preserves changed, unique and pinned evidence.
The [first research retrospective](../../../notebook/research-experience.md)
established the usefulness of shared input references, while warning that tooling
does not establish a study's result. Those observations motivated this review's
composition checks: repeated real input reads plus mutable output, actual
eviction/recovery, and useful progress after pressure. No prior review report or
author check was used as an oracle. While locating scope, the reviewer read the
study README, which included summaries of earlier reviews; those summaries did
not determine the workloads or expected results below.

The [harness](composed_workflow_v5.py) owns only
`build/reviews/storage-20261024/fresh-composed-workflow-v5/`. It uses the public
`flow.py run` CLI for input consumption. Independent expected hashes come from
deterministic in-memory member generation. Consumers hash actual mapped paths
and write receipts; read-only `debugfs` reads those receipts and protected bytes
from the unmounted image. Host `stat`, `losetup --associated` and `findmnt` check
image allocation and release independently of candidate accounting fields.
Direct `workspace.py run` calls seed explicit store pins/unique evidence and
generate/release disposable mutable output.

## Failure: a second job discards an earlier offline proof

The minimal `--phase overlap` case uses a fresh 16 MiB image and one cached
64 KiB input. Job A's archive contains `data.bin` plus its own `result.json`;
job B has the same `data.bin` and a different `result.json`. Both selected
acquisitions request only `data.bin`.

1. Acquire A online and verify its input bytes.
2. Rename A's archive out of the supplied backend path. A with its independently
   generated complete size/hash/policy manifest succeeds offline.
3. Restore A's archive, acquire B online and verify the same input bytes.
4. Rename A's archive again. The identical A manifest/selection now exits 125
   with `FileNotFoundError`, attempting to reopen A's absent archive.

Read-only image inspection finds exactly one cache object, with the expected
name and byte-for-byte 64 KiB contents. There was no capacity pressure, missing
payload or invalid hint. The full composed workload independently reproduced
the same behavior when a scoped artifact shared the first artifact's inputs.

`acquire()` checks the chosen objects' single recovery descriptor for the supplied
manifest proof and compressed archive identity. An online acquisition constructs
a new descriptor, and `Store.ingest_records()` replaces the existing descriptor
for that digest. B therefore removes A's previously verified membership proof,
although the content remains shared and usable. A needs a new archive scan to
restore availability. Preserving bounded proof information separately from the
single eviction backend, or explicitly declining offline cross-reference reuse,
requires a design decision; this candidate does not fulfill that useful behavior.

Relevant frozen source: `acquire.py` lines 376–405 and `store.py` lines 414–422.
Raw result: `results-overlap.json`, including successful offline-before-B control
and failed offline-after-B case. The missing-backend refusal itself is safe; the
failure is the loss of previously working offline access to retained input.

## Failure: existing v1 references exceed the parser's file limit

After the independent workload was designed, the owning task supplied a specific
compatibility concern. The reviewer then read only the existing compact recovery
reference map, `build/reviews/storage-20261024/operations/workers.json`, with no
archived payload access. Of 308 references, five have more than 4,096 members:

| Job | Files |
| --- | ---: |
| `20261017T085332Z-64ec7151` | 4,529 |
| `20261017T085332Z-9fdf565f` | 4,529 |
| `20261017T094617Z-4b737f96` | 4,525 |
| `20261017T094617Z-6c325e20` | 4,524 |
| `20261021T130039Z-b8b3dd4e` | 4,918 |

The `--phase compatibility` probe retains the exact required fields of the last
reference: format 1, 29,392,523 compressed bytes, 4,918 files, and compressed SHA
`dd117b74e334c5a2f5baa07f23e90051b86a3801580bfc3f1b5ff099ea0e6d7e`.
`acquire.py inspect` rejects it with `invalid v1 compressed SHA/bytes/file count`
before trying to open an intentionally absent archive. Changing only the count
to 4,096 reaches the absent backend, isolating the gate. The public flow uses
the same `reference_identity()` validation, so selected recovery cannot bypass
this total-member limit either. No network request or large synthetic archive
was needed. Raw result: `results-compatibility.json`.

## Completed useful work and pressure checks

| Workload | External observation |
| --- | --- |
| Default + six repeated job variants | Each consumer read correct bytes. Repeated names/shared content retained the same cache object set. |
| Selected compiler output | Explicit selection delivered its exact 1 MiB bytes. |
| Full acquisition | Delivered normal inputs, nested source package and ELF output; five unique content objects represented six archive members. |
| Default omissions | Nested source package and recognized large compiler output were omitted; a small ordinary text file named `notes.o` remained available. |
| Subdirectory reference | Delivered the exact members under relative scoped names. |
| Proven offline hint | Worked before another archive replaced its proof; absent archive without a hint refused. |
| Five distinct 1 MiB inputs through a 4 MiB cache | Every job ran and verified its bytes; later full recovery reproduced the original complete input set after eviction. |
| Unique/pinned/edited evidence | Unique 384 KiB bytes, explicitly lease-pinned recoverable 384 KiB bytes and a modified cached member survived five further jobs. Direct image reads checked them after every job and after the disk-pressure phase. |
| Oversize and changed input | A 4 MiB selection could not fit with metadata/protected content; modified member acquisition refused. Accepted evidence remained exact and a later ordinary job succeeded. |
| Real output ENOSPC | A bounded writer hit actual `ENOSPC` after 24,281,088 bytes in the 32 MiB image. A subsequent flow refused with `ENOSPC`. An earlier result receipt remained exact. |
| Recovery after disk pressure | Removing only the review's disposable output through a normal workspace command allowed a new flow to run and verify input bytes. |

The refusal cases are safety observations. The successful following jobs are the
separate progress observations. No test treats a sequence of refusals as useful
completion. Persisted manager logs append across runs, so their reported tails
can include earlier errors; per-command exit status and independently read
receipts determine the observations above.

## Reproduction, storage and limits

The ignored fixture retains the frozen source, small local archives/manifests,
`results-{ordinary,preservation,disk,compatibility,overlap}.json` and
`final-facts.json`. From macOS, with the frozen fixture present, run:

```sh
orb -m ubuntu python3 /home/ashtonsix/sixdb/workbench/spikes/storage-lifecycle/adversarial/composed_workflow_v5.py --phase overlap
orb -m ubuntu python3 /home/ashtonsix/sixdb/workbench/spikes/storage-lifecycle/adversarial/composed_workflow_v5.py --phase compatibility
```

For the complete main workload use a fresh main image and run phases `ordinary`,
`preservation`, then `disk`. The preservation phase intentionally changes cached
evidence; rerunning the ordinary phase against that changed image should refuse.
No implementation or other reviewer's files were edited.

The review allocated one 32 MiB main image plus one 16 MiB reduced-reproduction
image, meeting the 48 MiB aggregate image limit. Other fixtures occupied about
1.6 MiB. Both images were independently checked unmounted and free of loop
associations before their own disposable image files were removed. Compact
observations and deterministic reproduction code remain.

Untested here: power loss/host crash durability, simultaneous consumers, wrapper
or descendant lifetime attacks, inode exhaustion, privileged kernel bypasses,
host backing-volume exhaustion, real archive downloads, actual worker/build
integration, large historical payload parsing, and exhaustive malicious archive
cases. The assigned process/lifetime reviewer owns those separate adversaries.
The historical check establishes a metadata compatibility refusal, not that the
4,918-member payload would otherwise parse. No data loss or physical image growth
beyond its allocated capacity was observed in the workloads above.
