# Project marks

The family brings human gesture and technical clarity together. Expressive marks
sit comfortably beside precise diagrams and quiet typography. The guidance below
preserves their intent while leaving room for visual judgment.

## Visual direction

- **Consurgent rises together.** Distinct contributions support one another and
  produce progress. The three strands suggest gathering and spontaneous growth;
  the benefit of technology extends beyond those who build it.
- **Orbital suggests freedom from data gravity.** Its open flyby approaches,
  redirects and departs. The name also carries the Sky Lab association: orbit
  above the sky. Let the form suggest that freedom without needing a literal
  space scene.
- **SixDB carries a human gesture.** One continuous pen stroke begins at the
  lower left, varies in pressure and passes through a tiny apex loop. The loop
  fills into a stubby tip, while the reveal exposes its construction. A good
  drawing matters more than fidelity to the original signature.

The shapes should work before their symbolism is explained. Preserve purposeful
asymmetry and variation in weight, including SixDB's fuller upper-right terminal.
Refine the joins and changes in width so each feature grows naturally from its
stroke. Consistency follows the gesture; it does not require uniform dimensions.

Judge the family side by side. Compatible optical weight, softened terminals,
restrained colour and generous space hold distinct silhouettes together. Match
apparent presence at the intended size. Small-size corrections are part of the
design.

Motion develops the same ideas: SixDB writes, Consurgent rises together, and
Orbital flies past its planet. Coordinate opacity, growth and travel so the eye
can follow one developing event. Orbital's stroke leads attention towards the
appearing planet. Vary speed smoothly with the gesture, checking the whole rhythm
as well as critical frames. A widening terminal can appear to accelerate even
when path speed is smooth.

## Supporting material and review

Give readers a clear hierarchy and room to follow the idea. The Consurgent pitch
(`~/consurgent/pitch/`) uses quiet grids, rules and typography around expressive
marks; its diagrams use colour to distinguish meaningful elements. Carry that
clarity into technical figures, where arrows, geometry and labels must
communicate the actual relationship. Diagram semantics and validation remain in the
[documentation figures guide](../../../ikea/docs/images/README.md).

Compare a few deliberate alternatives at their intended size and in context.
Reference images help explore form; inspect the exported vectors and actual
animation before settling. Curvature checks, speed plots and opacity-weighted ink
area can explain a perceived defect, but their metrics are aids to judgment.
Recheck the full result after a local correction, including playback under load.

## Assets and use

| Mark | Assets | Editable source |
| --- | --- | --- |
| SixDB | [Static](sixdb.svg), [reversed](sixdb-reversed.svg), [reveal](sixdb-animated.svg), [small-size](sixdb-icon.svg) | [sixdb-pen.mjs](sixdb-pen.mjs) |
| Orbital | [Static](orbital.svg), [reversed](orbital-reversed.svg), [reveal](orbital-animated.svg), [small-size](orbital-icon.svg) | [orbital-pen.mjs](orbital-pen.mjs) |
| Consurgent | [Static](consurgent.svg), [reversed](consurgent-reversed.svg), [reveal](consurgent-animated.svg) | [consurgent-pen.mjs](consurgent-pen.mjs) |

Use Orbital's fuller small-size asset at 48px and below; its viewBox matches the
standard mark. SixDB's small-size icon uses heavier strokes and a square viewBox.
Reversed variants suit dark backgrounds; reveals and icons follow the browser's
colour preference. Reveals play once. Reduced motion and browsers without the
required CSS easing or path interpolation show the complete marks.

Each source defines its ink, drawing routes and timing. Shared
[geometry](geometry.mjs), [motion](motion.mjs) and [SVG rendering](svg.mjs) produce
self-contained, transparent assets with no JavaScript or font dependencies.
Regenerate and check from the repository root:

```sh
node workbench/design/identity/export.mjs
node --test workbench/design/identity/identity.test.mjs
```

Serve this directory over HTTP and open [preview.html](preview.html) to replay,
slow or step through the actual CSS animations, compare optical weights, and
inspect light/dark and reduced-motion rendering. Embed exports with `img` or
`object`; for multiple inline instances, pass a unique `id` to `renderMark` so
styles, masks and keyframe names stay independent.

[performance.html](performance.html) measures warm-cache playback under DevTools
CPU throttling. [Performance results](performance.md) explain the method,
retained measurements and limits of the comparison.
