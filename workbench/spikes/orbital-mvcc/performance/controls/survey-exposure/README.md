# Logical exposure of the first operating survey

This deterministic replay reconstructs the frozen initial shuffled cycles and
uniform starting ranks from the first survey. It computes distinct logical
rows and modeled unions of their complete 23-byte spans in aligned 64-byte
regions. It executes no database code and records no hardware service.

The largest dependent16 trace reaches 5304992 of 8388608 rows (63.24%). Its
complete-row span model covers 97.36% of the body's 64-byte regions. The model
excludes binary-search probes, metadata, input/output buffers, speculative loads
and prefetch. It does not extract compiled loads, record worker addresses or
measure reuse distance. The warm replay repeats the same trace.

[sources.json](sources.json) pins the generator and fixture at `58456a0`;
[receipt.json](receipt.json) records the reviewed replay and output hashes.
[output.json](output.json) retains all five measured read cases, their exact
logical counts and model assumptions.

To reproduce without writing into the repository, copy `replay.py` to an ignored
working directory and run it there with Python3. It writes `output.json` beside
itself. The asserted totals pin this historical trace; later generator changes
do not silently update the model. No new timing run is required.
