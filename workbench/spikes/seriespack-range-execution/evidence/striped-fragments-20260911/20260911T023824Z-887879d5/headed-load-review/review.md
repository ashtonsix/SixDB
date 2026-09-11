# Initial striped fragment: headed-load inspection

The initial implementation already skips inactive parts' body/head loads and
joins in the six inspected u64 callbacks. Source order alone therefore does
not justify the larger explicit-guard/full-fragment revision for these
lowerings. Proceed with the smaller candidate's bounded runtime comparison.

This is a local reconstruction of `initial/candidate.patch` against the accepted
encoder ABI source, compiled with the recorded Clang 21 full-x86 flags except
for source/output paths. `source-hashes.json`, `command.json` and
`compilation.json` retain those identities. All six function sizes and
instruction counts match the original `initial/codegen.json` entries.
`inspection.json` records that check. It does not establish whole instruction,
relocation or object equivalence with a future worker build.

| Target / payload width | Nonempty-interval branches before clipped part loads |
| --- | --- |
| AVX2 W5 | 0x1dc, 0x24d, 0x2d8, 0x35c |
| AVX2 W6 | 0x1e3, 0x252, 0x2db, 0x35f |
| AVX2 W12 | 0x1cf, 0x24c, 0x2e4, 0x377 |
| AVX512 W5 | 0x17d, 0x1df |
| AVX512 W6 | 0x185, 0x1e7 |
| AVX512 W12 | 0x164, 0x1da |

All entries use H16 and u64 output. AVX2 parts contain four values; AVX512
parts contain eight. Each branch bypasses both head loads and their joins for
an inactive part. W12 also bypasses that part's body load and join. For example,
W12 AVX512's first branch skips loads at 0x166, 0x16d and 0x17b; its second
skips loads at 0x1e5, 0x1f3 and 0x209. The adjacent assembly files show the full
control flow, including the separate full-fragment arm.

The shared 16-value residual window is loaded/decoded before clipping, and
the first part's residual expansion is also common work. Later residual
expansions are inside the corresponding live-part branches. The claim is
therefore about avoiding inactive body/head reconstruction, not eliminating
every instruction associated with inactive values. Wider in-tile residual
reads are permitted by the ordinary operation's admission; this says nothing
about arbitrary substituted sources or fault suppression by a prefilter.

No hardware timings, other carriers, ARM lowering or general performance
benefit are established by this inspection. The remaining window arithmetic,
clipped stores and loop state need the actual ordinary-call comparison.
