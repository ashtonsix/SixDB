# Exact-prefix large-source audit witness

[exact-prefix.inc](exact-prefix.inc) retains the corrected baseline audit: replay
the exact timed query prefix, check checksum and terminal cursor/nonce, and count
useful logical-source cache lines where the wire mapping is known. It is a source
fragment for the captured caller, not a standalone program or a cache-miss counter.

The [baseline findings](../../findings/baseline-20260911.md) retain the selected
results and measured worker artifacts. Recover the original overlay builder from
[the campaign archive](../../../../spikes/ikea-composition/archive/validation-20260911.md)
when reproducing that exact audit; adapting it to another caller requires checking
its query and layout contract.
