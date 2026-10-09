# Direct Domain routing control

Place the actual Domain immediately before each owner's slot array, instead of
storing a pointer to it there. This removes one dependent pointer load and 8 B
per Group or Window. It is a separate hypothesis; the preceding
[shared-header controls](../README.md) and main runtime remain unchanged.

Apply [`direct-domain8.patch`](direct-domain8.patch) after Checked8 Ready, or
[`direct-domain4.patch`](direct-domain4.patch) after Packed4 Word-loop Ready.
Adjacent cut files identify the exact bases and source hashes. Both use the
same checked `Result<Hold>` API and **8,388,607** live-use limit. The 8 B control
artificially uses that smaller limit for comparison; its original control still
has the wider counter range. Saturation refuses before mutation, including
through the actual Shape export consumer.

Each Group/Window now owns `StorageOwner<N> uses`, containing `{Domain,
slots[N]}`. Owner code uses `uses.domain`; ordinary slot access stays `uses[i]`.
Domain is 112 B, and slots begin at offset 112 for both N=1 and N=256.
`Domain` and those two StorageOwner types pass standard-layout assertions.
The surrounding Group does not: routing needs the StorageOwner offset only.
The immutable slot identity recovers the first slot and preceding Domain.
Packed4 carries its initial atomic word through routing and the checked CAS;
Clang's emitted AArch64 code confirms both that reuse and the missing header
pointer load. Pool-to-Lane access and the ownership check remain.

This still uses enclosing-object address arithmetic outside C++23 reachability
guarantees. Standard layout and sanitizer success do not make it portable;
`std::launder` cannot extend reachability. [P3407R1](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2025/p3407r1.html)
explains that gap and existing compiler practice. These are pinned Clang/Linux
controls, without a registry or alignment-mask assumption.

At 256 identities and 4 KiB native pages:

| Actual charged bytes | Direct8 A | Direct8 B | Direct4 A | Direct4 B |
|---|---:|---:|---:|---:|
| Group, including Domain and slots | 2,472 | 2,472 | 1,448 | 1,448 |
| FiniteGroup | 448 | 448 | 448 | 448 |
| Combined control mapping | 4,096 | 4,096 | 4,096 | 4,096 |
| Live bitmap capacity | 80 | 144 | 80 | 144 |
| Trim-span capacity | 1,272 | 2,192 | 1,272 | 2,192 |
| Discard bitmap capacity | 40 | 72 | 40 | 72 |
| **Populated public Pool total** | **8,408** | **9,424** | **7,384** | **8,400** |

Window is 320 B in both. The public charge still excludes the first 8 B
Group-vector capacity, setup and Object metadata; this is not the whole budget.

Two short, ordered sweeps on the local AArch64 OrbStack guest gave owner
capture + retain + byte read + two releases **22.20–22.51 ns** for Direct8 and
**23.04–23.32 ns** for Direct4. Foreign timings varied too much to rank layouts:
separate counters 7.5–29.6 / 7.8–24.5 ns, verified shared 64 B regions
42.3–45.3 / 43.8–83.7 ns. These lack distributions, PMU and database weighting.
Keep both for the composed target-worker comparison; the cheap address route
is established in generated code, a throughput benefit is not established.

The [qualification](qualification.json) records 32 existing native/sanitized
regressions plus 20 focused native/Debug/sanitized checks. They include
identity 255, checked count boundaries, mixed Group/Window, reuse, paused
publication and last-pin histories, growth, failures and saturated export
cleanup. Two root diagnostic TU compiles also passed against frozen e9926e1
Material; they are compile evidence, not a whole-driver run. The
[verified archive](artifact.json) holds the new source trees, receipts, recipes,
selected assembly and timing. Earlier controls and diagnostic source remain in
the preceding archive. No weak-memory or portable-address proof is inferred.
