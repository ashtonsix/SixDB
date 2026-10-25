# Fresh acquisition review v3 — 2026-10-25

The announced pair has one confirmed defect: the warm manifest path trusts
selection metadata which its proof does not authenticate. Default acquisition
can silently omit ordinary data, or include compiler output which the cold
default path omits. I would not treat default warm acquisition as equivalent
to verified cold acquisition until this is fixed.

## Frozen scope and independence

Reviewed only `store.py`, `acquire.py`, their CLI help and `AGENTS.md` before
constructing these fixtures. No previous review reports or authors' test
results were read or assumed. The explicit fresh-review scope superseded the
usual prior-research discovery step. Neither implementation file was edited.

Both files were copied before testing to
`build/reviews/storage-20261024/fresh-acquisition-v3/frozen/` and the executable
review checks these SHA-256 values before each run:

| File | Frozen SHA-256 |
| --- | --- |
| `store.py` | `5df915ee47486391b449661b01945c3afc1183069dd117292be80322992c89bb` |
| `acquire.py` | `ead261a602811b5c7d39063112997180dd78776ffd3631b3c78d4bd4ecc8dc5b` |

This reviews the local archive acquisition pair. It does not establish any
property of the separate enforcing-image wrapper, production integration,
outside writers, network backends or cloud storage.

## Confirmed finding: unauthenticated classification controls warm selection

`choose_members` uses each item's `rebuildable` value for the default selection
(`acquire.py:311`). The cached path passes the supplied manifest directly to
that function (`:331–332`) and later retains it as `members` (`:345–346`). Its
proof is computed through `validate_manifest`, which retains only `bytes` and
`sha256` for each name. Changing `rebuildable` therefore leaves the verified
proof unchanged. The path does not scan the archive when the chosen payloads
are resident.

The minimal fixture contains two ordinary ten-byte members, `a` and `b`.
After `inspect` and successful default acquisition of both:

1. Change only `manifest.members.b.rebuildable` from `false` to `true`.
2. Keep the artifact identity and complete genuine member size/hash map.
3. Make the local archive unavailable.
4. Acquire with that manifest and no selection flags.

Observed: exit status 0, `cache_hit: true`, `members: {"a": ...}` and
`omitted: ["b"]`. Expected default membership remains `a` and `b`, or the
unsupported hint must be refused. This also reproduces with the archive
available. Explicit full/selected requests were not shown to have this
classification defect.

There is an ordinary-input consequence too. The CLI documents `--manifest`
as independently supplied expected member hashes. Supply a genuine plain
size/hash map without `rebuildable` fields, after a full acquisition has made
all payloads resident. The default warm path returns a 1 MiB ELF compiler
member, whereas cold default acquisition omits that exact member. Default
selection therefore depends on cache state and optional unauthenticated
metadata, even without a forged hash or identity.

Any field used to omit data must be derived from verified content or covered
by a trusted persisted proof. A hash-only manifest must not silently provide
classification defaults that change verified selection. The implementation
owner can choose whether insufficient classification forces a scan or a safe
refusal; this review makes no implementation edit.

Minimal executable reproduction, from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/acquisition_v3.py --case manifest
```

It asserts that the counterexample exists; exit status 0 is **not** a passing
acceptance verdict. Both modes clear only their own `cases/` fixture subtree
beneath the assigned review root and leave the frozen pair unchanged.

## Other independent probes

Run the complete retained review with:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/acquisition_v3.py
```

| Scenario | Observation for this frozen pair |
| --- | --- |
| 64 changing jobs, three acquisitions per job, common bytes under a reference subdirectory | All 192 requests returned independently expected hashes/sizes; common payload had one CAS file across jobs/selections; metadata history was actually trimmed. |
| Cold default, selected source recovery and full selection | Expected independent member sets returned. Cold default omitted source packages and the 1 MiB ELF fixture. Full/selected inclusion worked. |
| Genuine offline manifest | Resident default payloads were acquired with the archive absent. |
| Wrong unrequested member hash in expected manifest | Refused even when requesting only the other good member. |
| Invalid unrequested archive entries | Refused traversal, absolute paths, symlink, hardlink, directory, sparse type, duplicate member, file/parent conflict, nonzero member padding and nonzero trailer. No payload was published. |
| Corrupted compressed CRC | Acquisition refused and published no payload. |
| SIGKILL after staging, after object publication, after catalog publication | No success output before the kill; restart reconciled scratch and subsequent acquisition returned both expected payloads. |
| SIGKILL during actual first output write | A test-only wrapper stopped `CheckedOutput.write` immediately after its first real write, without editing frozen sources. No object had been published; restart removed scratch and a normal retry succeeded. |
| Pinned and unique accepted data under pressure | Both 64 KiB independent payloads remained byte-for-byte intact through changing requests. Corrupting a released pin's recovery archive caused its valid local payload to be preserved. |
| Recovery of an evicted member from a CRC-corrupt archive | Refused; no replacement payload published. Restoring the original archive allowed correct recovery. |
| Changed resident CAS bytes | `get` refused and preserved the changed bytes for investigation. |
| Ten concurrent identical acquisition processes | All succeeded with one physical payload object. |
| Scoped reader plus competing acquisition | Reader consumed expected bytes; competing process remained blocked until the reader exited. |
| Kill scoped acquisition parent with a reader descendant that closes inherited descriptors | Independently identified host descendant terminated; a subsequent store request could proceed. |

## Independent allocation and byte accounting

Measurements use filesystem `lstat().st_blocks * 512` walks, direct file
contents and known fixture bytes. They do not use the store's allocation
report. A separate thread samples request allocation every millisecond;
stopped transaction points are measured directly. Sampling cannot rule out
sub-millisecond peaks, and process kills do not simulate power loss or storage
write reordering.

For the 64-job history run:

| Quantity | Observed bytes |
| --- | ---: |
| Configured managed-root budget | 393,216 |
| Independently expected acknowledged payload bytes across 192 successful requests | 4,194,304 |
| Distinct expected payload bytes across all jobs | 1,064,960 |
| Highest sampled managed-root allocation during a request | 315,392 |
| Highest allocation after a completed request | 290,816 |
| Final resident payload bytes | 262,144 |
| Final total managed-root allocation | 290,816 |
| Largest completed catalog file | 24,334 |
| Final catalog file | 13,817 |

The final history catalog retained 17 object records out of 65 distinct
payloads, demonstrating metadata retirement rather than only payload eviction.
All sampled managed roots stayed within their own budgets. The full-selection
fixture used an explicitly separate 2 MiB budget and allocated 1,077,248 bytes;
the corresponding cold slim fixture allocated 24,576 bytes. These are tiny
fixture observations, not a benchmark or comparison to the former copy path.

The final complete run retained 5,554,254 regular-file bytes and 6,660,096
allocated bytes beneath the assigned review root, including frozen copies and
all fixtures, below the 16 MiB ceiling. Machine-readable observations are in
that ignored root's `results.json`. No network, cloud, real cache, tool install
or benchmark build was used. No remaining work is claimed for this review
scope; the classification defect remains for the implementation owner.
