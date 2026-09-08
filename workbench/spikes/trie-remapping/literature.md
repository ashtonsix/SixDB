# Mechanisms related to trie remapping

Targeted reading, 2026-09-08. This is an initial map, not an exhaustive survey
or a novelty claim. The proposed SixDB combination is our inference; none of
these papers establishes its performance or production semantics.

**Bender, Demaine, Farach-Colton — Cache-Oblivious B-Trees (2005), section 2.2.**
The packed-memory array maintains ordered elements with gaps. Density bounds
at several scales decide how large a region to redistribute after updates.
The analysis bounds amortized movement; it does not make each insertion cheap.
[Author-hosted paper](https://erikdemaine.org/papers/CacheObliviousBTrees_SICOMP/paper.pdf)

Application: a principled alternative to periodic whole-segment respacing.
Adaptation questions are bounded segment size, payload width, publication,
and the cost of repairing other structures when labels move. The implicit
hierarchy over array density is not a second persisted search tree.

**Ding et al. — ALEX: An Updatable Adaptive Learned Index (2020), sections
3.2, 4.1–4.3 and appendix E.** Data nodes contain ordered gapped key/payload
arrays and occupancy bitmaps. Models guide placement and search, corrected by
exact search; inserts can shift toward nearby gaps, and full nodes can expand
or split. Appendix E explicitly compares this layout with packed-memory arrays.
[Paper](https://arxiv.org/html/1905.08898)

Application: particularly close to the proposed relationship between a logical
key and an assigned position. Its learned hierarchy is not the proposed
prefix-addressed physical trie. First compare exact suffix search without
learning; later test whether a predictor earns its build/repair cost. Ordered
placement and search should be evaluated together rather than choosing gap
positions in isolation. No published speedup transfers to SixDB by analogy.

**Wu, Ni, Jiang — Wormhole: A Fast Ordered Index for In-memory Data Management
(EuroSys 2019), section 2.** A prefix-based index over anchor keys locates
ordered leaf ranges; hashing accelerates traversal of the prefix metadata.
The paper explains why failing to match a prefix is not automatically proof
that the target key is absent from the leaf ranges.
[Author-hosted paper](https://wuxb45.github.io/papers/wormhole.pdf)

Application: the closest of these readings to discovering a remapped range
without descending a comparison tree from its root. It keeps B+-tree-like
leaves; SixDB would instead be testing physical trie positions inside the
range. Study anchor maintenance and exact range routing, not just lookup.

**Mao, Kohler, Morris — Cache Craftiness for Fast Multicore Key-Value Storage
(EuroSys 2012), sections 3 and 4.6.** Masstree combines tries with B+-trees over
fixed-length key slices. Border nodes use a compact permutation to expose
sorted order while inserting into an unused payload slot. Its publication
advantage depends on that small node layout; splits and slot reuse need
additional coordination.
[Author-hosted paper](https://pdos.csail.mit.edu/papers/masstree:eurosys12.pdf)

Application: compare moving a local ordering directory against moving payloads.
A permutation for a small node does not scale unchanged to 65,536 positions.
The trie/B+-tree composition is useful context but is not suffix remapping.

**Levandoski, Lomet, Sengupta — The Bw-Tree: A B-tree for New Hardware Platforms
(ICDE 2013), sections II.B and III.** A mapping table translates logical page
IDs to physical locations. Tree links use page IDs, allowing a page's bytes to
move without repairing every referring link. Delta installation and structural
updates use that indirection.
[Microsoft Research paper](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/bw-tree-icde2013-final.pdf)

Application: separate location management from ordering and ownership.
Page identity is not row identity: stable segment IDs alone do not preserve a
record locator across a split. This source does not supply SixDB's snapshot,
durability, or distributed transaction protocol.

**Leis, Kemper, Neumann — The Adaptive Radix Tree (ICDE 2013).** The abstract
proposes adaptive radix-tree structure to address memory and performance
limitations. This is a control worth inspecting further: adapting node layout
may solve some sparse-key costs while retaining natural keys.
[Author-hosted paper](https://db.in.tum.de/~leis/papers/ART.pdf)
Only the abstract was reliably retrieved in this pass; detailed layout and
algorithm comparisons remain reading work.

These suggest three separable comparisons: adapt natural-key node layout;
adapt the key-to-position mapping; or adapt payload placement behind an ordered
directory. They can share lower-level kernels, but their metadata and repair
costs must stay visible in [experiments](experiments.md).
