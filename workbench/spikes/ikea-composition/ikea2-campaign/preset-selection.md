# SeriesPack preset selection

These selection notes record the initial maintained surface. The current
[preset reference](../../../../ikea/docs/seriespack/representation.md#preset-policy)
owns the supported choices; the [candidate evidence](candidate.md) records comparisons.

The implementation retains 76 headless formats / 206 placements for explicit selection
and validation, but does not expose 206 named recipes. Borderline choices:

- Striped10/14/15 remain explicit and in ARM-oriented recipes. They do not earn
  an unconditional x86 default merely by winning isolated primitive cases.
- Cacheline spacing remains an explicit placement choice. Its locality benefit
  depends on siblings and access patterns; padding every object by default costs
  space and can hurt scans.
- One-bit extraction, complete-stripe writing, exact-width native stores and
  whole-body AVX-512 permutation earned inclusion through family-level gains.
- A separate named ScanPack/LocalPack facade, per-width threshold dispatches,
  additional tail dialects and automatic layout tuning are excluded from this
  initial surface. Explicit formats and the same native functions remain usable.
- AVX2 bit-mask packing earned a place for one/two-bit residuals. Four-bit
  packing did not benefit from the same treatment; a full native transpose
  remains the simpler basis. Measurements decide whether larger grouping helps.
- CPS is an execution choice, never a different container or physical format.
