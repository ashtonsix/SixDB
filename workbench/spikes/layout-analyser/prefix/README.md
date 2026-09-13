# Prefix-boundary consumer

This first native comparison holds strings at exactly 16 bytes and varies the
inline prefix and the position of a query's first mismatch. It isolates the
7/8/9-byte boundary; it is not yet a variable-length arena, NULL, patch or
collation experiment. The eight candidates are the two equivalent-record
families in [examples](../examples.md), each with a separate-prefix alternative.
The 55B and 57B other-fixed-byte families are compared within their own family.

The fixed region includes a scalar, length, 32-bit tail offset, other fixed
fields and a rare byte. Tails are dense fixed-size remainders in this fixture.
F's rare-byte plane is indexed by original row coordinate and uses a shared
base. C spends one byte on padding; B/E relocate useful string bytes.
All buffers begin at 128B alignment. `allocated_bytes` counts the actual
128B-rounded byte buffers, including small unused control allocations; it
excludes query/oracle fixtures and prepared-plan C++ objects. The recorded row
count and source definitions determine the useful byte count.

Operations return the same logical result within a family:

- Scalar reads use either an explicit byte load or a retained ordinary
  TuplePack reader. A >64B record need not be one TuplePack unit; this projection
  only needs the admitted first unit.
- Full-string-plus-rare and all-fields reads reconstruct the exact original
  fields. Physical offsets are used as addresses, not counted as logical values.
- Equality queries mismatch at byte 0/7/8/15 or match entirely (`mismatch16`).
  `mismatch17` mixes these at a fixed eight-query pattern: one match, one byte-0
  rejection, four byte-7 rejections, one byte-8 and one byte-15 rejection.
  Conditional tail visits are counted outside timing. These authored histories
  are stress cases, not estimates of production string distributions.
- Partial scalar writes vary the new value on every timed batch. The TuplePack
  arm returns actual source-qualified write coverage; the raw control omits
  that ordinary API/effects work. Compare layout rankings within a recipe.

`--check` validates every candidate against independent logical input, all six
equality outcomes, full/scalar projections and untouched fields across updates.
Checks also run in the shared captured runner. ASan/UBSan passed locally before
the initial hardware screen. Named plans/views stay at stable addresses.

The [runner](../run.py) records commands, compiler, hardware, source/binary
identities, seeds and exact row/query counts. It runs alternatives and repetitions
sequentially on one pinned CPU. Prefix candidate order is rotated by seed.
Each repetition measures batches until a minimum wall duration; checks and one
warm batch precede timing. Query IDs are uniformly generated with replacement
and then repeated, so allocation size alone does not establish memory residency.
The query/oracle input is identical across layouts. No internal fences or
software prefetch are added to the record operation.
