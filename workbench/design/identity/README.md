# Project marks

| Mark | Assets |
| --- | --- |
| SixDB | [Static](sixdb.svg), [reversed](sixdb-reversed.svg), [reveal](sixdb-animated.svg), [favicon](favicon.svg) |
| Orbital | [Static](orbital.svg), [reversed](orbital-reversed.svg) |
| Consurgent | [Static](consurgent.svg), [reveal](consurgent-animated.svg) |

SixDB is inspired by Ashton Six's signature. Its single pen gesture starts at
bottom left, turns through a tiny ink-filled loop at the apex, rounds the bowl,
and finishes at upper left. The favicon uses heavier strokes for small sizes.

Edit SixDB's curve, pressure and timing in [sixdb-pen.mjs](sixdb-pen.mjs).
Orbital and Consurgent use their static SVGs as masters. Regenerate from the
repository root with Node.js:

```sh
node workbench/design/identity/export.mjs
```

All assets have transparent backgrounds and no font or raster dependencies.
Reversed variants suit dark backgrounds; the favicon and reveals follow the
browser's colour preference. Reveals play once and show the complete mark when
reduced motion is requested. SixDB animates nine masks over fixed ink contours;
its shared timing curve is baked into CSS easing during export. Browsers without
CSS `linear()` easing display the complete mark.
Check intermediate frames and small-size rendering after curve or timing edits.

Orbital and Consurgent originate in the Consurgent pitch. Consurgent's reveal
comes from `consurgent/archive/site-v1/shared/consurgent-mark.js` and its draw CSS
in `site-v1/index.html`.
