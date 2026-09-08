# Bounds on survivors

Ashton's follow-up, 2026-09-08: **can a signature also bound the region of a
survivor that needs decoding?** This extends the [signature route](design.md)
from deciding which values reach RE2 to deciding which parts of those values
RE2 needs. The [raw-string study](experiments.md) implements conservative
intervals and verifies their union against RE2. It measures possible byte
reduction; no compressed bounded decoder has been measured.

## A second guarantee beyond survival

A Boolean signature promises not to reject a true match. Region extraction
must additionally preserve a complete opportunity to verify that match. Let
`W(x)` be candidate intervals in original decoded coordinates, with any
context needed to interpret them. The desired Boolean contract is:

```
R(x) = OR over w in W(x) of verify_original_context(R, x, w)
```

Each verification must imply a real match in the original subject, and every
matching subject must have a verifying candidate. Covering every possible
match span is a straightforward sufficient rule. A more aggressive rule that
keeps fewer witnesses needs its own completeness argument. A Boolean LIKE
match by itself supplies neither occurrence positions nor this guarantee.

## Where useful bounds come from

For the assertion-free byte regexp `id=[0-9]{4};`, a signature can locate
every occurrence of `id=` at decoded position `p`. Every possible match is
exactly eight bytes long, so only `[p, p+8)` needs verification, when that
interval fits within the value. In a long log message this may be much smaller
than the survivor. Coalesce overlapping candidates and stop after a confirmed
match. An `id=` occurrence near the end that cannot fit eight bytes is rejected.

For a required literal `v` in a matched branch `A v B`, compute conservative
minimum/maximum consumed lengths for `A` and `B`. At an occurrence of `v` at
`p`, with length `k`, possible match starts and ends satisfy:

```
start in [p - maxlen(A), p - minlen(A)]
end   in [p + k + minlen(B), p + k + maxlen(B)]
```

Clip to the real value boundaries and discard impossible ranges. With finite
maxima, `[max(0, p-maxlen(A)), min(len(x), p+k+maxlen(B)))` is a conservative
decode envelope for that occurrence. Initially lengths are bytes. Code-point
bounds need an explicit conversion and boundary handling for UTF-8; they are
not byte offsets. Assertions consume zero bytes but carry separate context.

An unbounded side can widen to the corresponding value boundary while the
other side remains useful. `id=[0-9]+;` has no finite maximum from syntax
alone, but occurrences of the terminating literal can supply candidate ends.
Retaining first/last compatible endpoints as a broad envelope is cheaper than
enumerating every pairing. Stronger class-aware scanning or a specialized
residual could tighten it further, with their costs charged explicitly.

Required prefix/suffix fragments, absolute anchors, finite repetitions, and
distances between required literals offer different amounts of information.
Bounds belong to a particular alternative: union candidates from OR branches.
Intersect compatible constraints for the same possible match, not arbitrary
occurrences of separate literals. A valid branch with no useful bound widens
the result, potentially to the whole value.

Repeated occurrences matter. In `id=xxxx; ... id=1234;`, the first signature
witness fails RE2 and the later one succeeds. Keep all relevant witnesses or
a conservative envelope over them. Truncating the occurrence list at a budget
can lose a real match; coalesce/widen or fall back to full-value verification.
Empty matches, nullable branches, overlapping literals, and unbounded gaps
can all remove the benefit without invalidating the Boolean signature.

## Decode intervals from known FSST positions

Have the compressed matcher optionally report a code-stream position plus
intra-symbol offset, and track original decoded positions using symbol lengths.
This lets a decode interval start from a known code boundary and expand only
its intersecting symbols. Boundary symbols can be expanded into small scratch
and clipped. Escaped bytes retain their escape state; never seek into an
arbitrary compressed byte and assume it starts a code.

FSST's per-string random access does not imply a free seek to any decoded
offset within a string. A matcher that only returns a Boolean may need another
code scan, length accumulation, or checkpoints to locate interval endpoints.
Backward matchers likewise need correct escape/boundary recovery. Charge that
work and any checkpoint space. Do not describe examining symbol lengths as
materializing the prefix, but do count the compressed bytes traversed.

Position reporting may prevent a Boolean matcher from exiting at its first
witness and may increase automaton state or metadata. Compare one enclosing
interval with a small union of intervals; a union saves bytes across long gaps
but creates more seeks and RE2 calls. Keep decoded intervals separate: joining
disjoint slices would create artificial adjacency and possible false matches.

## Preserve the original subject's semantics

Passing an arbitrary slice to RE2 as a new string changes its boundaries.
`^`, `$`, `\A`, `\z`, word boundaries, and multiline behavior can observe those
changes. For example, `\bid=[0-9]{4};` must reject an occurrence immediately
preceded by a word character even if the slice begins at `id=`. A slice also
must not manufacture start/end-of-text for an anchored regexp.

The first bounded route can support assertion-free byte search with proven
complete envelopes. Other cases require actual original-boundary checks,
adjacent character context, and a verified way to constrain candidate starts
and ends. Decode any required context along with the interval, preserving
UTF-8 boundaries in a text profile. A context byte added as padding is not by
itself a solution for absolute text anchors.

RE2's [matching API](https://github.com/google/re2/blob/main/re2/re2.h) takes
an actual uncompressed text view; start/end search positions are not an API for
a virtual, partly decoded original value. Inspect and test the selected API
or a context-preserving residual transformation before extending the bounded
route. Until then, use full-value decoding for unsupported context. Captures,
replacement, and leftmost-match reporting need additional ordering/offset
contracts beyond the Boolean existence question here.

## What would make bounds worthwhile?

Compare Boolean signatures with full-survivor decode against the same
signatures with position reporting and partial decode. Also keep a full-decode,
bounded-RE2-search control to separate decoding savings from reduced regexp
search. Hold the survivor set constant so fewer bytes is not confused with
better filtering.

Measure position collection, endpoint lookup, interval coalescing, decoded
context/overfetch, RE2 calls, and peak scratch. Vary long survivors with tiny
candidate regions, many false witnesses before a true one, disjoint regions,
and envelopes that cover nearly the whole value. A sensible policy can retain
the Boolean signature while declining bounds when locating and verifying the
regions costs more than decoding the survivor once. The
[experiment plan](experiments.md) includes these correctness and cost controls.
