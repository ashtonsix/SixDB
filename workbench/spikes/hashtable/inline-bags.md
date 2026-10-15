# Short multimap records

The earlier multimap control stores one full 64-bit value inline. Its second value
promotes both values into a StreamPack link. Ordinary Abseil's baseline bag has
room for two values inline. Native warmed short-bag updates currently cost about
5–6 times that baseline, so the inline/spill boundary needs a direct comparison.

This frozen 64-bit experiment admits one or two inline values while retaining the same
allocation, hashing, lookup, publication and linked-bag policies. It is an
implementation control, not a production option or a selected preset.

Two inline values occupy 16 payload bytes and three discriminator states:
zero means one inline value, one means an external bag, and two means two inline
values. Neither value loses any bits. Row/byte tags still occupy one byte;
packed tags require two bits. On the third append, reserve admits space for all
three values before any reachable record changes. The existing infallible commit
publishes the bag contents and then its record. Refusal preserves both inline
values. Deletion publishes the actual value-count delta. Lists and both bucket
tiers use the same states, including records in key-overflow links; promotion
transfers the bag owner and its representation-local references.

This policy spends another eight bytes per distinct key, including singleton
keys. At a fixed bucket unit size, it therefore reduces slot capacity and may
increase secondary/overflow traffic. It also avoids short-bag storage and the
associated owner metadata. The complete comparison charges both effects; an
isolated inline-append timing would not answer the question.

`hashtable_design_inline_formats` compares one/two inline values and a roomier two-inline bucket, each through
composed and direct bucket transfers, against ordinary and exact-width Abseil.
All use the same two-inline Abseil bag baseline, cardinality, bucket counts,
seeds and input history. The candidate uses admitted StreamPack append and the
existing permission to read within an initialized stable bucket. Exact arbitrary
slot capacities are solved separately for the 8- and 16-byte payloads. The fixed-budget hierarchy keeps 128-byte units. A third candidate restores at
least the original slot capacity by adding eight bytes per original slot and
rounding the unit up to 64 bytes. It remains a solver-admitted control, not a
selected growth policy; it can admit more slots than the original because of
that rounding. Both unit bytes and downstream populations must accompany times.

The original history starts with one value for two-thirds of keys and two for
one-third, then appends once to every key. Two inline values avoid spilling on
two-thirds of those updates, but the other third still spills. Additional
complete histories start with uniform fanouts 1, 2, 3, 4, 8, 32 and 128. Reads
consume every duplicate; a proved XOR cycle supplies dependent reads. Build,
append, deletion, release and logical allocated/owner memory are recorded beside
independent and serial reads. A phase sum is not an uninterrupted lifecycle.

Validation covers full-word duplicates, the third-value spill, refusal/retry,
packet order, key-overflow records, tag-bit neighbors, count deltas, independent
clones and actual ObjectStore retained readers. Native code generation must
qualify any claimed timing gain: increasing the inline record can change caller
fusion as well as data access. Local VM timings are diagnostic only.

The live implementation now generalizes the count through an inline byte budget;
see [value widths](value-widths.md). Its new width evidence must not be attributed
to this frozen 64-bit capture.

Remaining representation questions include qualification of wider bag elements,
skewed fanout, small-list lifetime and promotion thresholds, inline-capacity
selection with platform/growth geometry, shared-block reuse/compaction and
retained-version cost. This experiment does not close the broader set/map,
worker-growth, residence or heat requirements in the spike README.
