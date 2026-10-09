# Selected access with two real physical representations

This disposable caller composes [selected access](../orbital-selected-access/)
with the [SeriesPack/TuplePack fixture](../orbital-representation-conversion/).
It tests whether logical selection, physical interpretation, and actual native
bytes stay together across conversion, publication and waiting. It introduces
no Engine navigation, multi-block container or production interface.
The [cross-spike findings](FINDINGS.md) connect this caller to the proposed
structure and the independent critical-path experiments.

The fixture encodes eight columns as eight SeriesPack Parts, and the same
records as one TuplePack Part. The adapter uses `selected::capture` to retain
the fixture's immutable `Completed` closure and copy only Part descriptors.
Native `View::bytes()` must equal the original encoded body pointer. Per-Part
interpretation comes back through `part_interpretation`; `read_parts` then runs
the actual Ikea bodies on the qualified spans. Decoded values are checked
against independently generated input tuples, including zero/max/high-bit
anchors and a ragged 67-record case.

The caller is split into a compiled history TU and a small process/Pool harness;
it uses the owning spikes' public experiment headers only. The first integration
gap was the allocation-copying Builder: its byte-input signature could not
adopt an already completed Ikea representation. An owner-retained external Part
capture resolved this without creating a second copy of every encoded body.

`caller.cpp` exercises these consequences:

- Eight Parts become one at the same Object/version, and row-to-column
  conversion grows the Part count again; Part identity is local to the selected
  descriptor. Empty records keep these counts without any zero-byte allocation.
- A prepared logical edit still installs after an independent physical
  alternative is added.
- Conversion from captured v10 input may finish after v11 publishes, but the
  result is only selectable under v10.
- One ready column View is captured before a wait; further columns are
  discovered after conversion and publication under that same original
  selection. The resumed compound read still decodes v10. Completion, receipt
  and physical retirement remain distinct.
- A View cannot be retained under a different descriptor merely because both
  descriptors represent the same logical version. Wrong formats, versions and
  nonexistent Parts are refused.
- A target that fails after constructing one row has no completed owner to
  capture or select.
- Retained row and column access survives destruction of the Store, contexts,
  Slots and original fixture handles, followed by moves of the retained-access
  records. Both interpretation and bytes remain readable. Releasing column
  access expires its fixture owner while retained row access still reads: a
  resolved read whose complete dependencies are in the Representation need not
  retain the whole old logical State.
  Releasing row access then expires its owner; Pool close drains all charged bytes.

The wait is the selected-access spike's deterministic memory backend, not a
Port or scheduler claim. Conversion itself is synchronous: one history captures
its input before publication and executes it afterwards. It does not implement
an asynchronous conversion pipeline. Detailed late-completion, cancellation,
pressure and malformed-format cases remain with the owning spikes rather than
being replicated here. There is no performance result in this caller.

From the macOS/OrbStack checkout:

```sh
orb -m ubuntu cmake --preset dev -B build/clang/stage2-composition \
  '-DSIXDB_SPIKES=orbital-representation-conversion;orbital-selected-access;orbital-stage2-composition'
orb -m ubuntu cmake --build build/clang/stage2-composition \
  --target orbital_stage2_composition_check
orb -m ubuntu ctest --test-dir build/clang/stage2-composition \
  -R '^orbital_stage2_composition_check$' --output-on-failure
```

Validated on 9 October 2026, Linux aarch64, pinned Clang 21.1.8: the native
`-O2 -g` check and ASan/UBSan check both pass. All 27 project compilation commands
in the sanitizer target's build graph include `-fsanitize=address,undefined`.
Leak and stack-use-after-return detection were enabled; both build graphs had no
remaining work after testing. Routine CTest logs remain in their ignored build
directories, rather than creating a retained performance artifact.

The sanitizer build uses the same configuration in
`build/clang/stage2-composition-asan`, adding
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`. Run its CTest with
`ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
