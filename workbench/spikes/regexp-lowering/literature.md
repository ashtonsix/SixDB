# Reading and implementation references

Targeted reading, 2026-09-08. These sources inform the
[design](design.md) and [experiments](experiments.md); their measurements do
not establish SixDB performance. External source branches can move: pin exact
revisions when building an executable comparison.

## Where Calico left this question

Inspected sibling checkout at `ac83c82b9a0c8d76bea92829e471ebc0d98b1978`.
Links assume sibling `calico` and `sixdb` repositories. These observations
describe that checkout, not inherited SixDB requirements.

- [Regexp adapter and lowering](../../../../calico/foyer/src/regex.cpp),
  [interface](../../../../calico/foyer/include/foyer/regex.h), and
  [tests](../../../../calico/foyer/test/test_regex.cpp): `lower_regex` recognizes
  a nonempty literal with optional outer `^`/`$`, including some escaped
  punctuation. It emits EQ or LIKE; other patterns return FULL. It declines
  literal `%`/`_` because its LIKE has no escape clause. There is no signature
  result in this interface. The adapter selects Latin-1, case-sensitive
  unanchored search, with dot/newline disabled by default.
- [Binding](../../../../calico/foyer/include/foyer/bind.h): the REGEXP_LIKE path
  compiles/validates before lowering, preserving admission behavior. Exact
  lowering also supports the corresponding negated predicate. Validation and
  optimization are usefully separate concerns regardless of SixDB's API.
- [Predicate materialization](../../../../calico/foyer/include/foyer/predcat.h):
  GENERAL LIKE can prefilter with its longest literal run through a substring
  hook or raw blob scan, then verify candidates. This is a LIKE prefilter,
  not structural regexp signature extraction; alternation and optional regexp
  pieces cannot be treated as mandatory literal runs.
- [FSST leaf experiment](../../../../calico/engine/include/engine/fsst_leaf.h)
  and [R6 findings](../../../../calico/workbench/science/prior-art/r6-string-codecs/FINDINGS.md):
  the inspected FSST arm decodes entries for substring matching. Its historical
  cost does not rule out compressed LIKE. Per-segment tables, decoded-cache
  restrictions, format policy, and dictionary layout belong to that experiment.
- [R10 string-system survey](../../../../calico/workbench/science/prior-art/r10-string-systems/SURVEY.md):
  recorded the DaMoN compressed-LIKE paper as a follow-up whose primary text
  had not been read. This spike retrieved the author-hosted paper and located
  its public implementation, making a direct comparison possible.

## Compressed matching

**Pop, Riedl, Neumann — Compression-Aware LIKE: Matching Patterns in the FSST
Domain (DaMoN 2026).** [Author-hosted paper](https://db.in.tum.de/~pop/fsstlike.pdf),
[DOI](https://doi.org/10.1145/3789237.3809128), and
[implementation](https://github.com/calin2110/FSST-LIKE-Matching).

Sections 2–4 construct table-specific automata for literal prefix, middle, and
suffix fragments and compose them across `%` gaps. Symbol-internal positions
and escape handling are explicit. The construction uses greedy encoding and
a unique-three-byte-prefix property of its symbol tables. A compatible byte
format alone does not prove another encoder satisfies those assumptions.

Sections 5.1–5.2 separate decode/match throughput from preparation, including
compilation. Section 5.2 discusses amortization across blocks and shared tables.
This directly motivates varying table count and reuse; it does not require
SixDB to share tables. The presented fragment alphabet and examples do not
establish our desired `_`, SQL escaping, Unicode, or collation behavior.

Use the paper as a concrete compressed-matcher candidate. Its repository has
interpreted and generated-code paths; its listed LLVM16 and Vectorscan build
dependencies need assessment against SixDB's pinned compiler. Initial work
can isolate a matcher without adopting its whole benchmark/build system.
Neither the paper's speedups nor its matcher settle regexp lowering coverage.

**Boncz, Neumann, Leis — FSST: Fast Random Access String Compression (PVLDB
2020).** [Paper](https://vldb.org/pvldb/vol13/p2649-boncz.pdf) and
[upstream implementation/API](https://github.com/cwida/fsst).
The upstream description explains 1–8-byte symbols, byte codes, escaped raw
bytes, and independent string access. These are the representation details
the probe needs. Ordinary FSST compression/decompression does not itself
provide a LIKE executor; test matcher and codec together at explicit pins.
The [API](https://github.com/cwida/fsst/blob/master/fsst.h) also exposes symbol
lengths and expansions. Those can support locating bounded decode intervals;
arbitrary decoded-offset seeking within a value needs additional work. This
is our proposed use of the representation, not an upstream region-search API.

## Exact regexp rewrites

**DuckDB's regexp optimizer.**
[Documentation](https://duckdb.org/docs/current/sql/functions/regular_expressions)
and [implementation](https://github.com/duckdb/duckdb/blob/main/src/optimizer/rule/regex_optimizations.cpp).
The implementation examines RE2 structure and flags to choose literal,
prefix/suffix, and LIKE routes, declining unsupported shapes. This is useful
precedent for AST-based recognition and cheap specialized kernels. Its SQL
semantics and documented examples are not a proof for SixDB: check search
boundaries, newline behavior, and character consumption against our oracle.

**RE2's semantics.** [Syntax](https://github.com/google/re2/blob/main/doc/syntax.txt)
and [public API](https://github.com/google/re2/blob/main/re2/re2.h).
Use RE2 itself as the reference with recorded options. Match existence is
separate from capture extraction, and search is separate from full matching.
Do not build a second permissive parser that accepts patterns RE2 rejects.
Inspect availability and stability of the chosen AST interface at the pin;
using internal RE2 headers would be an explicit spike dependency.
For [bounded survivors](survivor-bounds.md), a slice is a different text view:
search limits do not provide a virtual original subject. Verify context and
anchor handling before claiming a partial-decode route preserves semantics.

## Necessary-condition extraction

**Zhang et al. — Exploiting Structure in Regular Expression Queries (BLARE,
PACMMOD 2023).** [Paper](https://pages.cs.wisc.edu/~jignesh/publ/BLARE.pdf) and
[code/data](https://github.com/mush-zhang/Blare). Splits regexps into string and
regexp components, verifies candidates, and selects among execution strategies
using a bandit learner. This closely relates to both survivor bounds and scan
adaptation. The published accident description/query pairing supplies one of
[our workloads](workloads.md). Its private production workloads are not in that
repository. Our replay uses rejection counts and explicit thresholds rather
than implementing its timing-driven learner or reproducing its speedups.

Application: compare direct RE2 with decomposition, retain all relevant
occurrences, and let observed work inform the method choice. The paper's
selection strategy does not determine a policy for independently changing
FSST tables, compressed matching costs, or an unrepresentative scan prefix.

**RE2 Prefilter and FilteredRE2.**
[Extraction source](https://github.com/google/re2/blob/main/re2/prefilter.cc),
[representation](https://github.com/google/re2/blob/main/re2/prefilter.h), and
[FilteredRE2 interface](https://github.com/google/re2/blob/main/re2/filtered_re2.h).
RE2 already extracts necessary conditions as AND/OR trees of string atoms,
maintaining finite exact sets where tractable and weakening when necessary.
Optional/star fragments yield an unrestricted condition; alternation uses OR.
This is a strong starting point for the signature algebra and a comparison
against writing a new extractor.

FilteredRE2 leaves string matching to its caller and returns lowercased atoms;
its interface requires case-insensitive searching or lowercased subjects.
Passing those atoms straight to case-sensitive compressed LIKE would be
unsound. Measure a semantically compatible adapter, or preserve original case
in a separate extraction implementation. The public wrapper targets filtering
sets of regexps; it is not a ready-made ordered LIKE lowering for one SQL
predicate. Atom presence also does not retain relative occurrence positions.

**Russ Cox — Regular Expression Matching with a Trigram Index (2012).**
[Author's account](https://swtch.com/~rsc/regexp/regexp4.html).
Derives required Boolean queries from nullability, bounded exact sets,
prefixes, and suffixes, with safe ways to discard information before those
sets grow too large. The application uses an index; our proposed leaves are
LIKE scans over compressed values. The algebra is relevant even without a
trigram index. Whether ordered LIKE fragments repay their greater execution
cost is a separate SixDB experiment.
