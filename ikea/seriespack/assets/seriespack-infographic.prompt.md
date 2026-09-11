# Ikea SeriesPack infographic

Generated with the built-in image generation tool from the current workspace documentation on 2026-09-11.

Sources: [Introduction](../../seriespack.md), [contract reference](../reference.md), and [layout definitions](../../include/ikea/seriespack/layout.h).

The full-tile byte count and hexadecimal encoding come from the introduction. The eight-value selected sum is 170. This is a representation overview, not benchmark evidence.

## Generation prompt

Use case: infographic-diagram
Asset type: a polished, shareable technical infographic for the SixDB repository.
Primary request: Produce an infographic for Ikea SeriesPack. Ikea is the SixDB software module, not the furniture retailer. Explain the implemented packed-integer component to a technically curious reader, using one concrete, correct example.

Canvas and style:
Create a high-resolution portrait infographic, approximately 1800 x 2400 pixels (3:4). Warm ivory background, near-black typography, disciplined technical editorial design. Use teal for whole-byte body data, orange for residual tail data, and restrained blue for execution. These are explanatory colors, not branding. Elegant bold sans-serif title, clean readable sans-serif explanatory text, monospaced numbers and byte values. Thin rules, crisp technical illustrations, generous whitespace. Flat vector-like rendering, no photographic elements, gradients, shadows, furniture, IKEA retail logo, watermarks, or decorative fake code. Make the visual explanation the focal point. Every label must be accurately spelled and comfortably readable.

Layout:
A strong title block, a large central packing diagram, three execution paths across one row, a concise three-column design explanation, and a small owner-contract footer. Use the exact copy supplied below. Do not add unsupported performance claims or imply a runtime benchmark. The 25% number is an exact byte-count comparison for a full tile only.

TITLE BLOCK:
Small eyebrow: "SIXDB / IKEA"
Large title: "Ikea SeriesPack"
Subtitle: "Packed integers for point access and native computation."
Small label strip: "1–64-bit unsigned values" and "Caller-owned storage"

MAIN DIAGRAM — about 40% of the page:
Section heading: "01  Eight values. Twelve bytes."
Small annotation: "Example: compact 12-bit format"
Show eight equal input cells in original order, each labeled with its decimal value:
7 | 19 | 42 | 0 | 4095 | 3 | 8 | 91
Caption: "8 × uint16_t = 16 bytes"
Use a neat downward transform arrow into a horizontal packet of exactly twelve equally sized byte cells, grouped into eight teal body cells and four orange tail cells. Put the hexadecimal bytes in the cells exactly:
00 | 01 | 02 | 00 | FF | 00 | 00 | 05 || B3 | B7 | 11 | D4
Over or under the two groups, braces label:
"8 body bytes" and "4 tail bytes"
Within a generous side annotation or directly under this packet:
"12 bytes per full tile"
"25% fewer bytes than uint16_t"
Explanation below, in two short lines:
"Body: the upper 8 bits of each value."
"Tail: four bit planes, each collecting one low bit from all eight values."
Include a tidy small point-read callout connected to the body cell 02 and the tail group:
"Read position 2"
"body = 2   tail = 10"
"(2 << 4) | 10 = 42"
Do not show a literal binary matrix; the byte ribbon and callout are sufficient.
At the base of this section, small but readable:
"Storage rounds up to full tiles: 10 values use 24 bytes."

THREE EXECUTION PATHS — a row linked from the same packed-byte diagram:
Section heading: "02  The same bytes, three ways"
Three balanced columns with clean technical pictograms (point cursor, unpacked array, processing lanes):
Column 1 heading: "Point access"
Copy: "Read or update one value."
Small code label: "get / set"
Column 2 heading: "Materialize"
Copy: "Decode a range into an ordinary array."
Small code label: "decode"
Column 3 heading: "Native composition"
Use a compact horizontal flow: "read → compare → sum"
Copy: "Decoded values can feed a consumer in registers."
Small example: "For this tile: sum(x < 100) = 170"
Make clear that this sum applies to the eight example values in the large packet, and that it sums matching values, not the Boolean predicate. If space allows render the example as "Sum of values below 100 = 170" instead, to avoid ambiguous mathematical shorthand. Prefer this unambiguous wording.
A fine blue line connecting the three paths should indicate common encoded storage, not sequential execution between these alternatives.

THREE DESIGN CHOICES:
Section heading: "03  Format, placement and execution are separate"
Three concise columns with small diagrams:
"Format"
"Compact tiles, striped layouts and optional 8/16-bit heads."
Use a few abstract bytes grouped into different arrangements.
"Placement"
"Dense or strided tiles in the owner's storage."
Show two solid tile rectangles separated by a thin hatched gap.
"Execution"
"Scalar, AVX2, AVX-512 or NEON on the same encoded layout."
Show one byte packet branching to several simple lane-width glyphs.
Short full-width note:
"Changing the reader does not change the bytes."

OWNER CONTRACT FOOTER:
Heading: "The owner supplies the context"
Copy in two concise blocks:
"Headless: count, format and placement metadata live outside the encoded bytes."
"The owner manages lifetime, synchronization and publication. Updates can share physical bytes."
Tiny source line, still readable:
"Source: ikea/seriespack.md · ikea/seriespack/reference.md"
Do not render local absolute filesystem paths.

Technical invariants:
This represents the current implementation, not future proposals. No automatic CPU tuning claims, universal speedup claims, zero-copy guarantees, lock-free updates, database transactions, or serialization guarantees. Do not label the split as separate head data: this example has a body and tail within one compact payload tile and no separate heads. The four orange bytes are transposed tail bit planes, not individual values' nibbles. Keep all eight input numbers and twelve hex bytes correct and in exact order. Spell SeriesPack with capital S and P.

## Targeted correction

Use case: infographic-diagram. Edit this existing Ikea SeriesPack infographic with exactly one targeted correction: replace only the diagrams and their small captions inside the bottom-left "Format" panel in section 03. Preserve every other part of the infographic, including all typography, layout, colors, numerical values, byte maps, headings, source line, dimensions, spacing and the first two text lines within the Format panel.

The current Format panel incorrectly illustrates single tile, two tiles, and strided placement. Replace that illustrative area with three compact conceptual format diagrams, labeled exactly "Compact", "Striped", and "Head split". Use small clean colored bands, not numerical byte-count diagrams. For Compact show body and tail bands next to each other (teal and orange). For Striped show a wider body band with a separate tail-stripe band (teal and orange), clearly schematic. For Head split show a small independent violet head plane above a teal/orange payload band. Label those tiny bands "head" and "payload" only if readable space permits. Keep these three examples within the existing panel boundaries with clear spacing. These represent encoding arrangements, not dense/strided placement. Remove the existing labels "Single tile (12 bytes)", "Two tiles (24 bytes)", and "Strided layout (conceptual)" from this one panel; do not change any equivalent text elsewhere. Do not add tile counts, size claims, gaps, or hatching in the Format panel. The neighboring Placement panel should remain exactly as is. Do not redesign or reflow the rest of the image.

## Final label correction

Use case: infographic-diagram. Make a single tiny technical labeling correction to this existing infographic. In the bottom-left Format panel, in the "Head split" schematic only, the lower horizontal band currently has a teal part labeled "payload" and an orange part labeled "tail". Replace that entire lower horizontal band with ONE unpartitioned teal rectangle labeled "payload" centered across its full width. Remove the orange tail segment, its internal divider and its label from this one Head split schematic. Keep the small violet "head" rectangle above the band. This is because the payload includes any body and tail; the tail must not appear as something outside the payload. Preserve every other pixel and all other text, diagrams, labels, colors, numbers and layout as closely as possible. Do not change the compact or striped schematics, the large byte map, the placement diagram, or any other panel.
