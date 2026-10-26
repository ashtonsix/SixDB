# Frame reference and extension seams

The ordinary contracts are in `<ikea/frame.h>`. `ikea::frame` is one compiled
library, linked to SeriesPack and Intervals. It has no Loom or Orbital dependency.
[Usage](usage.md) owns complete calling examples; the
[proposal](../../../workbench/spikes/frame-phase-3/PROPOSAL.md) records the decision
and qualified research strengths and weaknesses.

## Coordinates and admitted domain

| Term | Initial meaning |
| --- | --- |
| Value | Non-null unsigned 64-bit integer |
| Block | Up to 256 original rows; only the final block is short |
| Group | Up to 16 consecutive blocks, sharing placement/exception areas |
| Unit | Up to 256 blocks (65,536 rows), one checked reader's row domain |
| Stream | A stage subtree with its own cardinality and group sequence |
| Section | One stream group's contiguous immutable byte image |
| Part | An owner allocation identified by ordinal; can contain several sections |

`build` constructs one unit. A caller composes a whole column from units. Stream
row domains are explicit: the intervals field stream has the parent's rows;
its checkpoint stream has ceil(rows/G). The canonical graph describes the
coordinate mapping; the manifest records actual cardinality and placement.
A shared child cannot simultaneously own two different row domains.

Fine masks name original unit rows. Coarse masks name original blocks. Neither
changes coordinates, proves presence or grants read access. Borrowed selectors
can contain padding bits outside the unit: checked operations extract only the
logical extent. Insufficient selection backing refuses before result writes.

## Description and byte law

`description`, `node` and `edge` define a child-before-parent graph. Node vector
index is identity; edge order is semantic operand order. All nodes are reachable
from root. Canonical encoding preserves identities and sharing rather than
attempting graph-isomorphism normalization.

The common Ikea description envelope is tag `Fr`, version 1. Integer fields are
little endian. Body: domain:u8, block_rows:u16, group_blocks:u16, nodes:u16,
root:u16; then each node kind:u8, parameter:u16, heads:u16, edges:u8; then ordered
edges child:u16, mapping:u8. Limits are 256 nodes, 1024 total edges, two children
per node, block256/group16. The parser checks syntax, topology, coordinate/output
laws, exact length and limits before executable image admission.

Fr/v1 compact always uses SeriesPack Local. bulk_x86 uses Striped for payload
widths 1–7, 12 and 20; bulk_arm also uses 10, 14 and 15. The payload width is K-H.
This resolved map is part of Fr/v1 stored meaning, independent of future
SeriesPack presets. Width zero is a constant-zero leaf with no body, owned by
Frame's fitting/image law; positive-width bodies are existing SeriesPack bytes.

## Initial executable cells

| Shape | Build / immutable image / all ordinary reads | Fast laws |
| --- | --- | --- |
| pack (H0) | Supported | SeriesPack compiled exact SUM, selected/weighted SUM, COUNT/FILTER and points |
| bound → pack | Supported | Window/exception compensation for SUM, selected/weighted SUM and predicates; internal selected MAX |
| intervals_sizes(basic fields, basic checkpoints) | Supported | Exact weighted SUM at G16/G64; monotone >= COUNT through Intervals |
| intervals_relative_ends(basic fields, basic checkpoints) | Supported | Exact plain SUM of offsets; direct checkpoint+offset points; monotone >= COUNT |
| Other syntactically valid descriptions | Refused before payload access | None |

Here `basic` means pack or bound→pack, with distinct subtrees across the two
row domains. Both interval forms admit checkpoint spacing that divides 256,
from 2 through 256; G16/G64 have the tuned weighted paths. Other sizes-form
spacing decodes SUM. Full exact interval blocks take algebraic SUM; partial or
non-exact blocks reconstruct. Other comparison operators and unsupported fast
cells use their defined decode fallback. A law changes execution, never meaning.
The fused selected summary decodes each active block once for SUM/COUNT/MIN/MAX.

The graph syntax retains extension shapes for Bec-backed presence/rank,
TuplePack shared records/fields, block-local runs and fixed logical split.
H8/H16 are physical SeriesPack head choices in the description. None has an
initial executable Frame image law. Physical heads are relative to fitted K;
a logical split at a fixed bit index is a different child mapping. Unknown
shapes and incompatible domains refuse rather than becoming an H0/basic recipe.

## Fit and stored policy

`fitting_policy` contains exception penalty, per-block exception cap and per-group
slack. Its defaults are 0, 64 and 0. Capacity is recorded in emitted images; a
reader consumes that image metadata without re-running fitting. A changed
penalty or cap never changes old bytes' interpretation. Fit census reports
width/exception-bucket populations and interval evidence; it is not a calibrated
cost model. Measured, estimated and unknown costs remain distinct in Engine's
analysis. The first implementation does not choose a globally optimal recipe.

`built_unit` owns stream group vectors and fit census. `stream_images` retains
role path, root node identity, logical cardinality and group sections. Output
placement, copying, memory budget and owner publication remain caller decisions.
Construction may allocate privately; allocation failure returns `resources`.
The API does not promise a no-allocation fit or competitively priced point update.

## Placement, contents and binding

`validate_manifest` checks the complete metadata: executable description,
expected streams/cardinalities/group counts, owner ordinals, range-safe extents
and non-overlap. Unclaimed Part bytes are permitted. `attach_image` additionally
checks all supplied Part wrappers and every granted section key before resolving
any payload address. It reads no payload.

`validate_image` consumes only explicitly granted sections, copies interpretation
and metadata, and creates one immutable execution state. Each section must be
consumed exactly. The parent/leaf checks include width membership, slot order,
array lengths, ordered exception positions, representable inliers and zero
exception child fields. SeriesPack owns the positive-width byte law and storage
sizing. Frame proves the relationships its compensation laws assume.

An intervals root group becomes ready only when its checkpoint dependencies are
granted. No-shortcut reconstruction checks its stored last value, any asserted
monotonicity/exactness, and the zero-offset checkpoint fields. Logical decode
alone is insufficient: relative SUM includes fields ignored by point decode.
Conservative false evidence is admitted. Missing dependencies remain unavailable,
without dereferencing absent sections or validating other groups by accident.

`validated_image` and `reader` retain copied metadata and execution state, with
shared immutable proof across copies. They borrow payload, not the temporary
`image_view`/manifest/Part wrapper objects. The caller retains source storage,
read isolation and fixed addresses through every use. Rebinding changed bytes
or a new interpretation requires fresh validation. Publication and old-image
reclamation belong to the owner.

## Operation admission and results

`get`/`read` use unit row coordinates; `sum`, `count`, `filter` and selected
operations use coarse block selectors. `required_sections` and `reader.required`
return conservative point/read acquisition closures; selection and closure do not
grant bytes. COUNT/FILTER may settle unsigned-domain tautologies without payload.

Before any output write, checked calls admit the entire command: range-safe
coordinates, comparison, bitmap coverage, output capacity and all nonempty
payload dependencies. Refusals leave caller output untouched. Empty fine demand
removes its block's payload requirement. `filter` writes selected coarse slots
only and clears tail high bits. `select` writes increasing original-row order
and leaves the suffix beyond returned count untouched. Exact SUM is unsigned
128-bit; COUNT is uint64. A unit's totals fit those result domains. Empty fused
summary is `{sum=0,count=0,min=UINT64_MAX,max=0}`.

Outputs must not alias image bytes, reader state or input masks. These are trusted
borrowing/disjointness obligations, not an overlap scan in every call. As with
other moved-from resource objects, do not bind or execute a moved-from image or
reader. Frame performs no acquisition, suspension, locking or publication.

## Source and extension map

Header paths below are relative to `ikea/include/ikea/frame/`; source paths to
`ikea/src/frame/`. Ordinary headers contain contracts, without width-specific
kernels. All dispatch bodies compile once in `ikea::frame`.

| Namespace / header | Classes or entries | TUs and responsibility |
| --- | --- | --- |
| `ikea::frame`, `types.h` | geometry constants, `summary`, `error`, law coordinate arrays | Common coordinates/status; diagnostics in `reader.cpp` |
| `ikea::frame`, `description.h` | `description`, `node`, `edge`, canonical encode/decode | `description.cpp`: meaning and syntax; internal basic recipe admission |
| `ikea::frame`, `image.h` | `stream_manifest`, `image_view`, `validated_image` | `manifest.cpp`: placement/closure; `group_image.cpp`: basic parent/leaf proof; `image.cpp`: granted attachment and cross-stream proof |
| `ikea::frame`, `reader.h` | `reader`, `bind_reader` | `reader.cpp`: checked admission, selection adaptation, complete read requests |
| `ikea::frame`, `build.h` | `fitting_policy`, `stream_images`, `built_unit`, `build` | `build.cpp`: construct stages from admitted meaning, fit/emit one unit |
| `ikea::frame::author`, `author/stage.h` | `stage`, `fit_input`, `census`, `point_leaf` | `stage.cpp`: stream traversal and operation definitions via decode |
| `ikea::frame::detail`, `detail/stages.h` | pack/bound/interval factories; private concrete stages | `stages/pack.cpp`, `bound.cpp`, `intervals.cpp`: each stage's fitting, section law and algebra |
| `ikea::frame::detail`, `detail/execution.h` | internal read plan | `execution.cpp`: bind-time point entries; no separate public execution mode |

`sections.h` and `stages/windows.h` are private source helpers. SeriesPack owns
packing geometry/native kernels/exact reductions; Intervals owns search and
wide modular reconstruction. The owner example's Control/Physical/interpretation
retention is outside the Frame library and links Orbital separately.

A new stage adds its logical law and child domains, canonical description and
image admission, fit/emit/attach hooks and decode. Optional laws override whole
operations at unit/block grain. A new operation needs defined semantics and
independent conformance; decode supplies a fallback for supported shapes. No
binary plugin ABI or automatic acceptance of unknown descriptions is promised.
Known but unimplemented shapes remain described/refused until their image law
and operation semantics land together.

Mutation and checked private preparation are unavailable initially. Future change
support must separately describe logical destinations/contributions, issued byte
ranges, evidence invalidation, access grants and publication. No afterimage or
in-place protocol is fixed by the read/build landing; the research preparer and
its journal scratch are not a Frame dependency.
