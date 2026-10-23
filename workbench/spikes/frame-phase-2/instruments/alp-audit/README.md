# ALP/RD IEEE and floating-environment audit

The suspected RD dictionary overrun does **not** occur on the documented
complete primitive path. This pinned ALP revision does have separate failures
in ordinary `float` initialization and in `double` handling of sampled or
unsampled special/large values. Decimal decoding also depends on the floating
environment. The existing boot check is useful, but is insufficient to qualify
ALP as a general IEEE control.

Ikea HELPER owns this bounded correctness study for front A. It uses unmodified
ALP revision `31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce`; it changes neither
upstream nor the provider or existing codec controls. No speed comparison or
external bug report.

## Starting evidence and qualification

The existing [boot control](../codecs/alp_check.cpp) passed 18 primitive
cases per native profile, including bit comparisons for float/double and
special IEEE values. It did not instrument all executed upstream code. The
[corpus recipe](../corpus.py) preserves original IEEE binary64 values instead
of the scaled-integer Jena representation used in phase 1. The
[representation route](../../../../notebook/representations-and-planning.md)
retains the separate question of exact aggregate laws; this audit concerns
bit-exact codec round trips, not floating SUM.

Upstream's pinned [primitive guide](https://github.com/cwida/ALP/blob/31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce/PRIMITIVES.md)
specifies 1024-value vectors, initialization per rowgroup, two FFOR stages for
RD and a final exception patch. We test that composition and retain an
omitted-stage negative control. We respect the sampler's chosen route; a
non-nearest environment can select RD even for the decimal fixture.

[The executable](check.cpp) and all four linked upstream FFOR/unFFOR translation
units use pinned Clang 21.1.8 on native Linux AArch64 NEON, with ASan, UBSan,
float-cast-overflow checking and strict dynamic floating arithmetic. The
archive hash and all 775 extracted files match the official archive before and
after the experiment. [The receipt](qualification.json) records the source,
commands, executable, corpus, results and linked inspection.

There are 144 subprocess cases: 117 bit-exact results, three bounds diagnostics,
five floating-to-integer overflow diagnostics, twelve signed-overflow diagnostics
and seven bit mismatches under changed floating environments. Expected failures
are observations retained by the audit, **not passing upstream qualification**.
No upstream file, provider or production Ikea code was modified.

## RD: active dictionary size is not allocation size

The [RD source](https://github.com/cwida/ALP/blob/31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce/include/alp/rd.hpp)
decodes dictionary indices before patching exceptional rows. Its
[state](https://github.com/cwida/ALP/blob/31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce/include/alp/encoder.hpp)
allocates and zero-initializes **eight** dictionary entries. With an active
cardinality of five, entries 5–7 are inactive but still initialized array
members. They are not out-of-allocation reads.

Raw encoding can emit an exception code of eight or nine. However, the
[generated FFOR body](https://github.com/cwida/ALP/blob/31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce/src/fastlanes_generated_ffor.cpp)
masks every code to the chosen left width. With a valid initialized state that
width is at most three. The unFFOR output therefore indexes 0–7; patching then
restores the original exceptional high bits.

Actual full-vector examples for double (float has the same results):

| Case | Raw exception code | Code handed to decode | Result |
| --- | ---: | ---: | --- |
| Eight active entries, all exceptions | 8 | 0 | Valid entry; patch restores all 1,024 values |
| Five active entries, all exceptions | 5 | 5 | Initialized inactive entry; patch restores all values |
| Sampled key mapped past the eight retained entries | 9 | 1 | Valid entry; patch restores all values |
| Omit FFOR/unFFOR on the left | 8 | 8 | UBSan array-bounds failure before patching |

The last row is a violated primitive precondition, not a failure of the complete
RD composition. Do not omit packing in a purported equivalent RD control.
Likewise, restoring only active dictionary entries requires the rest of the
actual eight-entry state allocation to remain initialized as upstream supplies
it. Arbitrary or corrupted state is not qualified here.

All **1,106 RD vectors (1,132,544 values)** round-trip bit exactly:

- Active cardinalities 1–8, with no, one, 1,023 or all 1,024 exceptions, for
  both float and double. Training uses sampled vectors; later input can differ.
- Sampled exception-map indices beyond the retained dictionary, plus positive
  and negative zero, infinities, signed quiet-NaN payloads, a signaling NaN,
  subnormals, normal boundaries and maximum finite values.
- Every combination of the four rounding modes and AArch64 flush-to-zero
  off/on for the special-bit fixture. The codec works on representations.
- All 1,024 vectors of the 16 original binary64 corpus prefixes: 65,536 rows
  each, 8 MiB total. Jena sentinels remain numeric; nothing was quantized or
  cleaned. These bounded prefixes are not population claims.

Independent expectations compare original IEEE representations and exception
high bits. The witness checks exception position uniqueness, code truncation
and the allocated bound before invoking the unchanged decoder. The outlined
linked decoder was inspected: its dictionary load really precedes the
exception patch; the sanitizer bounds check is present. Four-byte witness tail
stubs were followed to the actual upstream bodies.

## ALP: three separate source-qualified failures

These use documented automatic initialization and encoding, without forcing an
inapplicable decimal route or constructing malformed state.

| Trigger | Actual diagnostic | Source mechanism |
| --- | --- | --- |
| Initialize `float` on ordinary decimals between −0.5 and 0.49 | `decoder.hpp:129`, factor index 10 outside `int32_t[10]` | Sampling tries factor 10, while the float factor table has entries 0–9. |
| Initialize double with sampled ordinary magnitudes or `-0`, infinity, NaN or maximum finite value | `decoder.hpp:129`, signed integer overflow | Decoding multiplies signed integers before conversion to floating point; either an ordinary encoded value or the safe encoder's sentinel can overflow. |
| Initialize on clean double decimals, then encode an unsampled `-0`, infinity, NaN, maximum finite value or mostly-special vector | `encoder.hpp:88`, floating-to-integer overflow | The encoding loop's unchecked conversion can run before exceptions are patched. |

The clean double fixture passes its complete ALP integer pack/unpack and decode
path with zero exceptions under round-to-nearest and gradual underflow. That
positive control distinguishes the failures from a broken harness. Naive
round-trip checks on optimized, uninstrumented code can still pass despite
undefined behavior; the old boot results do not disprove these diagnostics.

The first original 1,024-value vector of each corpus column was also offered to
automatic ALP. Eight pass bit exactly; eight fail during sampling with signed
overflow, including ordinary Jena pressure and active-power values. For example,
the pressure prefix reaches `997 * 1000000000000000000` in the signed multiply.
This is not confined to exotic IEEE values. The ALP corpus observation covers
these first vectors; RD's separate corpus result covers every vector.

The same [source and commands](run.py) reproduce each failure separately, so a
first diagnostic does not hide another case. No upstream workaround is included
in a result presented as an unmodified ALP control.

## Floating environment is part of the decimal control

Inputs are first created under round-to-nearest with gradual underflow, then
held as raw expected bits. The experiment controls encoding and decoding
rounding independently. For this 1,024-value double decimal fixture:

| Encoding rounding | Decode nearest | Decode upward | Decode downward | Decode toward zero |
| --- | ---: | ---: | ---: | ---: |
| Nearest | 0 mismatches | 512 | 502 | 602 |
| Upward | RD, exact | RD, exact | RD, exact | RD, exact |
| Downward | RD, exact | RD, exact | RD, exact | RD, exact |
| Toward zero | 240 | 260 | 471 | 0 |

Upward/downward initialization chose RD. Those cells run the real RD fallback;
they are not evidence of decimal ALP behavior in those modes. Same-mode success
on this fixture does not prove all inputs safe under that mode.

A separate nearest/nearest fixture contains the smallest positive binary64
subnormal. With AArch64 FPCR.FZ off, it becomes one exception and is preserved.
With FZ on, it produces no exception and one output differs: the subnormal was
lost before exception detection. RD preserves the corresponding bits in both
environments. Compilation uses `-fdenormal-fp-math=ieee`; that flag alone does
not neutralize a changed hardware FPCR.

Decimal operations also raise sticky inexact flags, and the gradual-underflow
subnormal case raises underflow. The witness restores the caller environment;
it does not claim upstream preserves flags. Enabled floating traps are outside
this experiment. A fair decimal panel should state and hold its rounding,
gradual-underflow and exception environment for encoding **and** later decoding.

## Consequence for Frame's controls

Use the named, complete **ALP_RD** primitive path for the audited general-IEEE
panel. Label automatic float ALP and the failing double cases unsupported at
this pin; retain their failure cells. A restricted decimal-double panel can
remain useful on actually qualified inputs with a controlled environment, but
its scope must be explicit. Silently dropping special values or calling the
modified decimal subset “general IEEE” would give Frame an unfair control.

If a future full-input driver routes rejected rowgroups to RD, that routing,
input inspection and fallback must be visible and priced. This audit neither
implements that policy nor selects a replacement upstream revision. It gives
front A the failures and a usable RD composition to build on.

## Reproduction and limits

From the repository root, on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/spikes/frame-phase-2/instruments/alp-audit/run.py
```

The script verifies the existing pinned archive/extraction and the prepared
corpus, incrementally builds in `build/frame-helper/alp-audit/sanitize`, and
records every command and observation under its sibling `evidence/`. It makes
no download, worker request or timing run. `--source`, `--build` and `--corpus`
select existing local paths; source revision and corpus dimensions stay checked.

This is native NEON qualification at the recorded compiler and flags. It does
not cover fused `falp`, the upstream whole-file compressor, partial-vector
padding, corrupt inputs, native x86 execution, all possible IEEE bit patterns,
other compilers or speed. The four executed upstream FFOR/unFFOR units are
instrumented; calling this a fully instrumented build of the entire upstream
project would overstate its scope. Rebuildable binaries, logs and disassembly
stay in ignored storage; committed source and the compact receipt retain the
findings and meaningful negatives.
