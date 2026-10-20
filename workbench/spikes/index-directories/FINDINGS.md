# Directory and producer checkpoint, 2026-10-20

The current-root graph and bounded producer now have useful throughput signal.
They do not establish complete Engine acceptance. The immediate priority is
removing measured allocation/metadata waste while retaining these outputs,
snapshots, atomic publications and throughput. Ordered mutation, balancing,
extras/schema and general segment/record codecs remain implementation work.

## Actual composition and the read result

One actual Orbital Scope selects the root; its physical interpretation proof
owns the immutable directory children. Reads follow qualified byte pointers
without a catalog selection per child. The SAME live Scope protects separately
versioned material. No previous-root ownership chain, Engine MVCC, undo queue or
authoritative shadow map is involved. Writes to the graph pay real path copies,
child-owner retention, captures and publication; read throughput does not price
those writes. Source bytes and that trade-off are in [Design](DESIGN.md).

Pinned Clang21.1.8, ARM/OrbStack, guest CPU8. Each read consumes all eight actual
material words into XOR, using the same ready flat-u32 trace. Scope/root/Part
selection and close occur inside each64-request cohort; native LMDB renew/reset
and cursor renewal are included. Calico uses real prepared runtime calls on
unchanging data, without a retained-old-cut API. Inputs, full-byte/locator
oracles, per-input batch association and all timed signatures were independently
audited. These are reciprocal-throughput observations, not request latency.

| Dense segment count | Scalar graph ns/item | Staged32 graph | Native LMDB u32 get / cursor | Best tested Calico full8 visitor |
| ---: | ---: | ---: | ---: | ---: |
|8,192|4.750|4.741|147.965 /150.764|85.978|
|262,144|38.043|29.338|391.095 /374.649|143.905|

The staged graph adds about1.30x throughput over scalar at262K;8K ties. That
control combines staging, fewer call boundaries and amortized checks. The much
larger recovery over the earlier repeated-catalog caller also changes physical
topology and useful-consumer/input closure; it is not an isolated MLP gain.
An earlier262K stable-name dependent projected read lost784.9ns to native540.5ns;
its moving-Part update was7.91us against6.82us. Preserve those negative probes,
without presenting unlike operations as a clean before/after timing pair.

Against the best tested full8 Calico control, the graph gives18.1x/4.91x useful
throughput. The2x complete-request planning envelopes are42.989/71.952ns/item;
subtracting the present graph consumer leaves38.249/42.615ns/item. This is
conditional budget arithmetic, not measured time for missing Engine work or
permission to add costs from unrelated trials. The initial25% directory/binding
allocations are21.495/35.976ns. Actual directory attribution must still separate
the material consumer, and the complete composed request must be measured.
No p99, cold/history, durable, range, write or native Zen5 win follows here.

## Actual memory is the principal defect

Primary warmed backend-only UNTRIMMED RSS/PSS, MiB, with external live input and
trace buffers released. Released allocator capacity remains counted:

| Dense segment count | Engine | Calico recordwise | LMDB native-u32 |
| ---: | ---: | ---: | ---: |
|8,192|9.215 /7.031|8.793 /7.424|6.867 /5.357|
|262,144|176.613 /174.421|24.758 /23.386|46.080 /44.575|

Engine's262K lifetime high-water mark was211.83MiB; its steady RSS is about7.1x
Calico's. Roughly4MiB of freed trace storage remains allocator residue. LMDB's
load-and-serve environment retains about20MiB of live load-era heap plus20MiB
resident DB file; close/reopen minimum footprint was not measured. Do not call
that46MiB its irreducible footprint or compare file size with Engine RSS.

The source/counter audit reconciles132.32MiB of native mapped payload:
16MiB actual material,17.29MiB directory/groups/graph,51.03MiB class rounding,
48MiB mapped-page underfill. Alignment64 forces the old palette choice; the
interleaved16-material/one-directory allocation cadence underfills class pages.
The32-lane512-byte stack bank is negligible. Native reserved VA4.90GB is sparse
reservation, not resident RAM; page-table cost remains separately reported.

Requested live PMR metadata is27.22MiB INSIDE43.00MiB pool backing, not additional
to it:14.90MiB captured headers/address/ownership slots,12.13MiB catalog/hash
structures and0.19MiB graph proofs. Another15.78MiB is pool rounding/chunks/spare
capacity. Pool mapping, backing, live requests and RSS are different ledgers.
The one-record-per-segment fixture preserves actual segment cardinality;
reductions must not silently merge segment Objects or remove their association.

Orbital owns eligible-palette/page-placement correction. Engine owns caller
metadata retention and redundant Control fields. The next measured comparison
keeps64-byte alignment, full Object identities, all output bytes and actual
physical lifetime/publication rules. It must report both real footprint and
read/update/refusal/reclamation consequences, rather than moving accounting.

## Full-cycle producer and the stronger eager control

The finite DRIVER has64 actual rows, eight u64 fields, all five summary layers,
SUM128 and field3 assignments. Plain append writes24-byte absolute afterimages;
indexed append also copies a160-byte footer; packed eager clones one576-byte
correction/SUM image. Ordinary updates change zero directory facts. Partial/full
folds publish actual moved-Part locators with material. Failed suffixes rotate
from the selected committed prefix, never overwrite sealed bytes.

Frozen sources buffered1/2/3 retain the iterations. Eliminating unnecessary
144-owner setup/destruction, full-row scans and repeated advice admission moved
hotB1 plain from412.6ns to247.0ns in the first screen. The follow-up pairs plain
against frozen2 again:243.39 versus243.32ns, a tie. This does not attribute the
earlier gain to one instruction. Vector copying reduced indexed append overhead;
the complete source revision, not an isolated copy benchmark, is measured.

Final foreground plus declared maintenance, medians in ns/ATOMIC REQUEST:

| Trace | Plain | Indexed | Packed eager | Native get+put | Native cursor CURRENT |
| --- | ---: | ---: | ---: | ---: | ---: |
|B1, hot2, no query|243.4|253.9|318.9|3070.8|3015.0|
|B1, hot64, query/fold/refusal pressure|851.3|536.4|561.2|3990.2|3830.3|
|B64, hot2, repeated-key coalescing|310.4|380.6|385.5|3263.3|3120.4|
|B64, hot64, query/fold/refusal pressure|3888.4|2183.3|2033.0|11521.8|7617.9|

In the last row, whole-history ns/command are62.37/35.80/33.23 versus180.89/121.17.
Packed eager beats indexed append there. Its predecessor used separate small
row/SUM Objects and cost25.11us/request; replacing dozens of captures with one
actual image reduced that to2.03us without weakening atomicity. That older loss
does not establish an eager lower bound. Plain remains best in the hot cases.
Choose from whole fixed-policy traces; do not splice each policy's fastest piece.

The indexedB1-pressure wall median536.4ns differs from owner/process about506.5ns;
all samples, including506..557ns wall spread, are retained. It is not clean
evidence of the same percentage instruction reduction. Other medians have CPU
close to wall; CPU includes stalls. No sampled request tails were collected.

There are262,144 supplied commands per run, either one or64 per publication;
native batches may coalesce repeated rows too. Each arm occupies every position
once across independent processes,5repetitions(6with the paired baseline).
Across the two screens,222processes/444phase rows passed full useful-output and
final64-row oracles; source/binary hashes remain fixed. Full-history clocks begin
before backend setup and include fixture/catalog destruction and actual Worker
shutdown. They also include final oracle/reporting overhead, explicitly labeled.
External command/oracle preparation is outside both arms' full-history clocks.

Pressure cases have8 full-row reads/request; SUM every64B1 requests or16B64;
partial+full fold every4096B1 or256B64; failed prepare+retry every8191B1 or511B64.
Native abort/retry is charged but has a different physical failure cause.
LMDB uses actual64B values, BE4 keys and CPU-only NOSYNC/NOMETASYNC. It is not
forced to reproduce Engine compaction. SUM uses a renewed ordinary cursor walk.
The stronger numeric-key write alternative remains separately qualified,
unmeasured here. Matching Calico mixed updates/SUM128 or atomicB64 is still open;
the earlier8K scalar Calico assignment658ns is not substituted for a matched64-row
trace. These are meaningful producer controls, not full Engine acceptance.

## Verification and remaining scope

Four current native checks pass: directory bytes/slot versus rank, physical
graph/late history/export/faults, the earlier P/Q/P composition, and current
plain/indexed/packed-eager all-field/all-layer producers. Tests cover repeated
atomic batches, no-op omission, partial/full folds, actual capture/prepare/global
stale-basis refusal, prefix rotation, SUM overflow, late first old reads after
drain, exact-end0/8..512-byte copies and physical shutdown. Broader concurrency,
general NULL/aggregate laws, arbitrary segment codecs and durable publication
are not implied. The current model reports only literal backend sequences;
copy/append cycle estimates exclude cache/TLB/branch/callee/allocator work.

The rest of the spike remains owed: memory solution and matched regression,
ordered mutation/balancing/extras/schema, varied realistic segment/row counts,
placement/duplicate pressure and complete Engine comparisons. Read-only speed
and a finite producer do not close those requirements.
