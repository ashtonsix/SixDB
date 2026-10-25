# Fresh acquisition review, v4

Independent review on 2026-10-25 of the local archive acquisition candidate, using
generated disposable files. No implementation changes were made. No other
reviewer's report or author-test conclusions were used as the oracle. The actual
`store.py` and `acquire.py` CLIs were read, then copied before testing.

The [research retrospective](../../notebook/research-experience.md) established
shared input references as a useful mechanism, while the [storage guide](../../tools/storage.md)
describes the existing limits as retention budgets rather than quotas. Those
findings set the starting question: does this candidate make ordinary slim,
selected and full acquisition useful without one input copy per job, and do
failures preserve accepted bytes? The [artifact packer](../../tools/artifacts.py)
writes regular-file tar members, so the fixtures use that subset, including PAX
long names. No existing worker, cache, build tree, dataset or cloud service was
used as test material.

## Frozen subjects and execution

| File | SHA-256 |
| --- | --- |
| `store.py` | `4f0169e68785aa1dc5cbef1e7ce8ce269312b5f8585e897acb18dce5f7fa47d1` |
| `acquire.py` | `023bc2f88f0474bc41b9bea3f3d0281ea2157137bc13aeab11394dac1cbd511a` |

The frozen copies and compact machine results are in ignored
`build/reviews/storage-20261024/fresh-acquisition-v4/`. Python was 3.13.7 on the
OrbStack Ubuntu host. The retained [harness](adversarial/acquisition_v4.py) at
completion had SHA-256
`a61483e1873f06e606dbdf977c26f08a10615d7253698aecb02bafa7e1495448`.

Commands actually run from the checkout:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/acquisition_v4.py \
  --work build/reviews/storage-20261024/fresh-acquisition-v4
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/acquisition_v4.py \
  --work build/reviews/storage-20261024/fresh-acquisition-v4 \
  --case corrupt-backend-healthy-alternative \
  --case metadata-retirement --case simultaneous-admission
```

The second command added three new cases to the first nine; it did not rerun
existing cases. The final harness contains all twelve. A full replay needs a
fresh `--work` directory; use `--source` pointing to the frozen directory above
if the live candidate has changed.

Fixtures had a 32 MiB allocation cap, checked throughout by a separate recursive
`lstat().st_blocks * 512` walk including directories. The final recorded fixture
allocation was 7,266,304 bytes. Expected payloads came from deterministic generated
bytes, with independent sizes and SHA-256 hashes. Scoped consumer subprocesses
read `SIXDB_INPUTS_FD`, checked exact member names, opened the supplied paths and
hashed the contents. The candidate's own `used()`/stats value was not the
allocation or content oracle.

## Observed defect

**Corrupt archive recovery can escape the regular store CLI's error handling.**
The `recovery-mutation-and-recover` case acquired a 65,536-byte artifact member
into a 262,144-byte store, mutated a compressed archive byte, then attempted
`store.py fetch` of a 131,072-byte independent recoverable file. The call refused
and preserved the accepted local member, but emitted an uncaught traceback
ending in:

```text
store.Refused: archive recovery failed verification: Error -3 while decompressing data: invalid stored block lengths
```

This differs from the CLI's structured `{"error": ...}` refusal. Running
`store.py` as a script defines `Refused` in `__main__`; its archive backend imports
`acquire`, which imports a second `store` module and raises that module's
`Refused`. The traceback crosses `admit()`'s exception handler. The harness
asserted a nonzero return code and retained the error text; it did not record
the exact inner return code for this call.

After restoring the archive bytes, pressure admission succeeded, the old member
was independently observed absent, and `store.py get member:DIGEST --lease
recovered` restored the exact expected bytes. This experiment found an error
composition defect; it did not find accepted-byte loss.

A follow-up with an older corrupt backend, a healthy backend and a larger
incoming object **did not establish** the conjectured stronger failure that a
corrupt backend prevents admission despite a healthy eviction alternative.
That store CLI call returned zero. A subsequent acquire control also succeeded;
final content checks found the corrupt backend's cached payload intact, the
healthy payload evicted and the incoming bytes correct. Do not treat the
stronger blocked-admission conjecture as a result.

## Completed cases

All twelve cases completed their encoded assertions. The negative corruption
case's nonzero-return assertion does not make the traceback acceptable CLI
behavior.

| Case | Independently checked outcome |
| --- | --- |
| Ordinary shared slim acquisition | 18 scoped jobs shared one 65,536-byte input inode; each read its expected input, measurement and source file. Root/nested source packages and a 1 MiB ELF member were omitted. Peak managed allocation 172,032 / 524,288 bytes; 20 retained CAS files. |
| Selected/full/subdirectory | A `study/` reference recovered an explicitly selected source package and then all four scoped members, including a 1 MiB ELF file. Final allocation 1,077,248 / 2,097,152 bytes. |
| Wide mapping and offline hint | 700 long member names, sharing one payload, were consumed through the descriptor map. Names alone totaled 134,400 bytes. With the archive temporarily absent, a previously authenticated complete manifest permitted a warm cache hit and correct reads. One CAS file; 20,480 / 262,144 allocated bytes. |
| Manifest authentication | Offline hints changing an omitted hash, omitted name, selection policy, compressed reference hash or omitting policy were refused. Accepted content remained unchanged. These checks establish that the altered hints did not obtain offline acceptance, not successful cold recovery from an absent backend. |
| Malformed archives | Traversal/absolute names, symlink, hardlink, duplicate member, file/parent conflict, bad gzip CRC and truncated gzip were refused. No payload object was published in these fresh stores. |
| Recovery mutation and restore | Corrupt recovery under pressure preserved the cached payload, with the CLI traceback described above. Restored archive bytes allowed verified eviction and subsequent recovery through the store CLI. |
| Local cached-byte corruption | Acquisition refused a modified CAS file and preserved the changed bytes. It did not launch a successful consumer with those bytes. |
| Publication kills | SIGKILL at `after-stage`, `after-object` and `after-catalog`, followed by a fresh store CLI, preserved a pinned accepted member. Before catalog publication, new bytes were removed as unacknowledged; after publication, new bytes survived. Staging and pending journal were cleared on restart. |
| Reader wrapper kill | A live consumer blocked a competing pressure admission and retained its readable bytes. Killing the acquisition wrapper released the scope; pressure then completed and evicted the old payload. |
| Corrupt backend with healthy alternative | The proposed stronger availability counterexample did not reproduce; store CLI returned zero. Final accepted content remained correct, as qualified above. |
| Metadata retirement | 96 acquisitions in a 262,144-byte store reached a 122,880-byte peak. The last inspected catalog had 13 keys. The first alias was retired and subsequently reacquired from its independent archive. A unique unarchived anchor survived every operation. |
| Simultaneous admission | Five concurrent scoped acquisitions each read their own 102,400-byte expected payload. All completed. 174 external path-allocation samples peaked at 126,976 / 262,144 bytes. |

Publication-kill allocation observations, before and after restart:

| Hook | Before restart | After restart | New member retained |
| --- | ---: | ---: | --- |
| `after-stage` | 86,016 | 49,152 | No |
| `after-object` | 86,016 | 49,152 | No |
| `after-catalog` | 86,016 | 81,920 | Yes |

## Limits

This is evidence about the exact frozen local regular-file backend and CLI
composition. It does not establish production integration, filesystem quota
enforcement, power-loss durability, cloud transport behavior or arbitrary outside
writer control. No production build or worker task was launched.

The allocation oracle sees pathname-reachable allocated blocks. It does not
measure filesystem inode/journal metadata, unique CoW extents or unlinked open
files. Concurrent allocation was sampled rather than observed continuously.
The reader case killed a wrapper with an ordinary waiting consumer; it did not
exercise daemonized/forked descendants, deliberate descriptor closing or namespace
escape. Actual ENOSPC/inode exhaustion, archive mutation during an active read,
wide maps with many distinct payloads, malformed catalog repair, huge expansion
and every parser extension were not tested here. These limits remain explicit;
safe refusal alone was not used as evidence that ordinary acquisition works.

At the parent's request, this review stopped after reporting the completed
tests. Candidate repairs and their independent replay belong to the parent task.

## Repaired-cut replay, 2026-10-25

At the parent's explicit request, a narrow replay checked the repaired archive
cut. The original frozen subjects and results above remain unchanged. The new
copies and minimal replay results are under ignored
`build/reviews/storage-20261024/fresh-acquisition-v4/revision3/`.

| File | Replayed SHA-256 |
| --- | --- |
| `store.py` | `b0a4c22be48aba7a19bd9bd61d86e30b90392bcc7999cd7a824175753122b771` |
| `acquire.py` | `7ff36f2abf2ef6e84d9fdaf544af89793aa6851ba51852b8519dc49927d32970` |

Command executed:

```sh
orb -m ubuntu python3 build/reviews/storage-20261024/fresh-acquisition-v4/revision3/replay.py
```

The replay reused the retained harness's fixture and external consumer helpers,
without changing either implementation. Its allocation check covered the
original and revision3 fixture trees together, under the same aggregate 32 MiB
cap. Final recorded aggregate allocation was 8,794,112 bytes. The replay script's
SHA-256 was `e6e2bc7d3bfbdebded6b4cbfb44d8c417056c45c6de83cefe48961a01ef03f7b`.

- The same corrupt-recovery pressure case now returned **2**, with one parseable
  JSON `error` describing capacity refusal and the failed archive verification.
  There was no traceback. The accepted 65,536-byte cached member remained exact.
  Restoring the archive again allowed pressure eviction and exact store-CLI
  recovery. This checks the observed error-composition defect's repair.
- The healthy-alternative probe again returned zero from the store CLI. Its
  subsequent acquisition control succeeded, with the corrupt backend's cached
  bytes intact, the healthy payload evicted and the new payload correct. The
  stronger blocked-admission conjecture remains unestablished.
- Actual bare filenames `members`, `artifact`, and both together were each
  tested with a plain member map and an envelope returned by the actual
  `inspect` CLI. The inspect members were compared to independent expected
  names, sizes, hashes and policy. For each of the six combinations, cold
  acquisition reported `cache_hit: false`; then acquisition with the archive
  temporarily absent reported `cache_hit: true`. Twelve scoped consumers
  independently opened the supplied paths and checked exact name sets and
  contents. No filename was confused with the manifest envelope.

This replay did not repeat the broad original suite, test the new composition
driver or exercise the new `outer_guard` API. The original scope and measurement
limits above still apply; no broader deployment conclusion follows from this
narrow repaired-cut result.
