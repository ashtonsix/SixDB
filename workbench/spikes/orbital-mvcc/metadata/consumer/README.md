# Consumer packets and qualified reads

An 8-byte borrowed Tuple cursor is concrete. It is one encoded-body pointer,
not an owner or a pointer to an allocated cursor box. Count, format law and
backing protection still have actual costs. The stateless canonical experiment
uses an explicit 8-byte count control; its pointer+count alternative is 16 bytes.
Neither establishes an 8-byte Object or complete physical closure.

These are disposable consumer experiments against the rejected Stage2 baseline
`7f73c81`. They introduce no production API or container/navigation policy.
The [cross-module investigation](../README.md) owns the collection-wide metadata
target; the [performance study](../../performance/README.md) owns timings.

## Census and ordinary ABI

The [field/call census](ikea-census.md) distinguishes stored plans, placement,
ownership, payload and construction/update effects. The [116-record CSV](packets.csv)
contains actual target sizes, alignments, traits and ordinary return classifications.
The [write/conversion call audit](codegen-write-control.md) identifies checks,
result copies and binding work that actually survive compilation, with their
stationary view and owner-closure requirements.
[probe.py](probe.py) produces real `sizeof` executables, LLVM signatures and
assembly from independent TUs using pinned Clang 21.1.8 and libstdc++15. The x86
size executable runs under QEMU; that execution supplies no timing evidence.
The ARM target is `armv8-a+simd/neoverse-v2`; x86 is `x86-64-v3/znver5`.

| Baseline record | AArch64 / x86 bytes | Ordinary return on both | Residence/use |
| --- | ---: | --- | --- |
| Tuple cursor | 8 / 8 | Registers | Borrowed body address |
| Tuple control | 16 / 16 | Registers | Schema reference and count |
| Tuple body / generic ReadData | 24 / 24; 56 / 56 | Indirect | Homogeneous native / generic conversion |
| Four Series cursors / body | 32 / 32; 40 / 40 | Indirect | Four independent addresses plus explicit count |
| Owning Representation | 8 / 8 | Indirect | Nontrivial ownership; size alone does not predict ABI |
| `Result<TupleCursor>` | 16 / 16 | Registers | Checked borrowed access |
| Native Tuple endpoint / its checked result | 16 / 16; 24 / 24 | Registers / indirect | Local reader/view references / cold binding result |
| Schema | 10,176 / 15,296 | Indirect | Shared full preparation state |
| Tuple constructor | 8,000 / 13,120 | Indirect | Two retained wide-writer plans, one unused for this law |
| Four Tuple readers / four writers | 384 / 384; 1,344 / 1,344 | Indirect | Read and update plans |
| Private WriteViews / owning Writers | 328 / 328; 192 / 192 | Indirect | Construction/update frame records |

Twenty-eight of the 116 probed types return in ordinary registers on each target.
This is a type census, not dynamic call frequency: cold vector-owning Shapes,
large SIMD plans, effect arrays and logical payload must not be treated as
equally hot return packets. Ownership and error wrappers are probed separately.

## Concrete comparators

| Candidate | Per-cursor/value | Explicit shared or call state | What changes |
| --- | ---: | --- | --- |
| Full-Schema direct | Cursor8 or pointer/count16 | TupleControl16 and Schema10/15KiB | Avoids generic ReadData56/Result64 qualification temporary |
| Compact read law | Cursor8 or Part16 | ReadLaw64; Control16 for cursor, law argument for Part | Keeps four transfer controls after full preparation Schema ends |
| Canonical stateless law | Cursor8 or Part16 | Count Control8 for cursor; no retained law instance | Derives fixed controls in code, using the existing Ikea transfer kernel |

Full-Schema comparator state is deliberately separate: `candidate_default_schema.cpp`
defines the optional shared default Schema, and is linked only for its comparator.
Direct/compact/canonical probe TUs have no extra global Schema constructor.
The compact law is shared across matching Parts, not separately allocated per Part.
Its one-cursor retained law/control/cursor total is 88 bytes; law plus Part is 80.
The canonical cursor and count total 16 bytes; materializing its empty tag would
add `sizeof(Law)==1`. Fixed comparison constants and generated code remain real
program costs, measured separately by [candidate_probe.py](candidate_probe.py).
No source type size is asserted to be an optimized frame size.

The separately compiled hot-only point/chain probe charges generated code too:

| Hot law probe | AArch64 text bytes | x86-64 text bytes | Emitted rodata |
| --- | ---: | ---: | ---: |
| Full Schema | 480 | 665 | 0 |
| ReadLaw64 | 440 | 563 | 0 |
| Stateless canonical | 552 | 909 | 0 |

These are two exported calls and any emitted helper in each isolated object,
before linked deduplication. They exclude cold admission, backing metadata,
unwind sections and the enclosing application. Canonical code is larger in this
probe; removing retained controls is not evidence of a performance improvement.
The combined compact/canonical ABI object also emits 107 bytes of rodata on
each target, including its cold descriptor/parent comparisons; its text is
3,636/5,287 bytes. That object exports multiple mechanisms and comparators, so
those totals are not the incremental cost of one chosen application path.

Canonical controls specialize exactly this 23-byte Tuple encoding. Cold admission
compares every actual 75-byte descriptor byte and checks multiplication, exact
extent, null/overflow addresses. It does not trust benchmark Corpus constants.
All reads invoke Ikea's existing `tp::detail::read_gpr_transfer<1,Bytes>` body.
Both compact paths are experiments at its private authoring seam, with no
general-purpose preparation contract or copied payload load algorithm.

The direct 8-byte result is 16 bytes and returns in ordinary registers. Checked
pointer/count16 results are 24 bytes and return indirectly; the full-Schema
status-plus-output variant returns 8-byte status and commits output only on
successful qualification. The [cut1 code audit](codegen-control.md) separates
those ABI facts from actual emitted frames and deeper callees.
The [compact/canonical code audit](codegen-candidates.md) verifies ordinary
returns, retained-plan loads, cold checks and the actual optimized caller frames.
Canonical point/chain reserve no ARM stack and 8/24 bytes on x86 in this probe;
these are emitted function frames, not total application-stack bounds.

## Parent proof and lifetime

[descriptor_adapter.h](descriptor_adapter.h) retains rich `Description88` admission
as a control and adds immutable borrowed parent inspection. Both preserve actual
Object/version, ProgramId, logical count, canonical 24-byte parent, actual one-Part
directory and requested PartId checks before the direct Part validator.
[compact](compact_descriptor_adapter.h) and [canonical](canonical_descriptor_adapter.h)
selected adapters preserve the same facts without retaining a Schema.

The [generic addressed-Part adapters](adapters.h) support explicit requested IDs
under a many-Part selection, but prove only that Part's descriptor and placement.
Their enclosing parent/shape proof is a separate obligation. A legacy resolved
View cannot reveal actual sibling count: its richer legacy overload explicitly
requires prior whole-parent admission. A real extra-Part capture demonstrates
Selection/Representation refusal and that View's information limit.

Native use requires matching admitted count/law, sorted encoded keys, protected
immutable backing and output disjoint from encoded bytes and live metadata.
No cursor retains its former control/view/frame address. An actual escaped Part
uses Orbital Export ownership. Export retains its entire frozen metadata slab,
dependencies and selected supplied backing, including any umbrella ownership.
Those remain additional costs; the small native packet does not erase them.

## Qualification and measurement limits

Frozen candidate controls are saved under
`build/orbital-mvcc/metadata-consumer/qualified-cut-{1,3}`. Cut1 keeps full-Schema
complete calls and the ReadLaw64 mechanism distinct. Cut3 adds borrowed parent,
compact and canonical complete calls, and the real extra-Part falsifier.
Exact sources, commands, linked libraries and results belong to each local
qualification receipt; current source is not silently substituted into older
codegen or timing evidence.

The six cut3 checks cover exact descriptors and count bounds, independently
decoded wire bits, independent linear logical lookup/digest Outcomes, empty and
high-key boundaries, long and cyclic chains, nonzero spare bits, moved borrowed
packets and escaped local controls. Native complete-call evidence includes:

- 99,076 compact calls and 123,684 canonical calls after preparation Schema death.
- 24,700 calls per path after original Allocation release with a real Hold.
- 600 complete compact/canonical calls composing full selected admission with
  Schema/input-description death and actual Export-only protection.
- 126 composed refusals for bad selected identity, missing/extra Parts, wrong ID,
  bad Part descriptor and truncated body; both full-Schema parent controls also
  reject malformed physical closures.

The native and ASan/UBSan/LSan/always-use-after-return cut3 configurations pass
all six checks with assertions enabled. The linked cached lower libraries have
118 source hashes matching the frozen baseline and prior qualified build;
each receipt records actual library hashes and separately compiled Object TUs.

Construction uses the real frozen encoder. The composed test reports both actual
metadata resource and external backing-owner/payload charges and requires final
zero charge; its additional copied read law is test state, not hidden candidate
storage. These checks do not implement MVCC qualification or supply timing claims.
Complete calls consume rows immediately into a caller-owned Outcome24, with no
Record[16] payload scratch or shared scan dispatcher.

For collection accounting, construction Schema must actually end before the
compact/canonical resident phase. Using a canonical read function while retaining
the benchmark's full Schema still charges 10/15KiB. Shared format/shape proof must
be admitted against real immutable descriptions and associated with the physical
realization before repeated serialized metadata or checks can be removed. Orbital
owns that representation/layout comparison; the existing descriptor is a bulky
control, not the collection-wide target architecture.

Run the source/type census or candidate codegen without a timing campaign:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/consumer/probe.py \
  --source-root build/orbital-mvcc/metadata-consumer/frozen --frozen \
  --output build/orbital-mvcc/metadata-consumer/probe --cpu 2
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/consumer/candidate_probe.py \
  --candidate-root build/orbital-mvcc/metadata-consumer/qualified-cut-3/metadata/consumer \
  --baseline-root build/orbital-mvcc/metadata-consumer/frozen \
  --output build/orbital-mvcc/metadata-consumer/cut-3-codegen --cpu 2
```
