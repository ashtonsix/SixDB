# Project marks

The SixDB mark is a compact symbol derived from Ashton Six's signature. The rising sweep,
upper crossing and rounded loop retain its gesture. Full strokes, softly rounded
terminals with restrained taper, and shaped joins draw on the Consurgent and
Orbital marks. The signature's accidental bottom kink is removed.

- [SixDB](sixdb.svg) and [reversed](sixdb-reversed.svg): used in the
  [top-level README](../../../README.md).
- [Orbital](orbital.svg) and [reversed](orbital-reversed.svg): the mark from the
  Consurgent pitch, used in [Orbital's README](../../../orbital/README.md).
- [Consurgent](consurgent.svg): the original mark from the Consurgent pitch,
  retained here in version control.
- [Animated Consurgent](consurgent-animated.svg): the corrected draw animation
  from the archived Consurgent site, with synchronised gusset reveals.
- [Favicon](favicon.svg): the same contours centred in a square, with ink colour
  following the browser's light or dark preference.

The SixDB mark is a single compound path with an open counter, built from
editable cubic Bézier curves. All marks have transparent backgrounds and no
font or raster dependencies. Edit the masters directly, then regenerate the
reversed, favicon and animated versions:

```sh
node workbench/design/identity/export.mjs
```

README headers select the warm white versions for dark themes and use the
warm black masters otherwise.

The animation comes from `consurgent/archive/site-v1/shared/consurgent-mark.js`
and the matching draw CSS in `site-v1/index.html`. This is the version with a
shared origin at `(100,180)` and side gussets widening from 11 to 15 units at
the base. Each gusset has a 16-unit reveal mask on its strand's centreline,
using the same animation timing; the base does not thicken after the stroke
has appeared. The earlier `site-v0` animation has offset origins and no gussets.
The standalone SVG preserves the 1.4-second draw and 80/160-ms stagger, and
shows the complete mark when reduced motion is requested.
