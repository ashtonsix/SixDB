# Earlier Orbital drafts

The [current brief](../BRIEF.md) is the design direction. These documents preserve
earlier thinking, including proposals that were replaced. Their instructions,
completion criteria and implementation claims are historical material. Use the
[mining list](../MINING.md) to find ideas worth reconsidering.

| Source | What it contains | Useful places to look |
| --- | --- | --- |
| [Early narrative](BRIEF.md) | The broad systems vision: witness hierarchy, deterministic execution, extensions, networking and durable memory. | The later paragraphs on sandboxing, memory projection, logical connections, compression and lessons from Calico. Much of this material predates the contention studies. |
| [Early structured brief and working notes](BRIEF2.md) | The retained-lock C1/C2 design, preparation retries, and appended discussion. This is **not** the BRIEF2 promoted to the current brief. | “Consensus — Multiple Shards”, the worked example, and the notes on sharing analysis and opaque application semantics. |
| [Consensus specification](CONSENSUS.md) | The 18–19 September specification, before the contention redesign. | Numbered sections on durable submission, admission metadata, continuation identity, acknowledgement paths and recovery. Its model-building instructions are no longer an active assignment. |
| [Arbitration brief](BRIEF-arbitration.md) | The former `orbital/BRIEF.md`, preserved unchanged when the new brief was promoted. | Component-owned protection, retry rounds and dynamic preparation. This is the baseline for the original scenario workbench. |

The names are historical, not version numbers to follow in order. There is a
second naming trap: “BRIEF2” in the
[contention comparison](../../workbench/spikes/orbital-scenarios/COMPARISON.md)
means the former compute/promote/renew candidate. The current brief instead
fixes transaction positions before execution and releases allocation access
before computation. Historical policy names and evidence remain unchanged.

Automatic data-member elections, emergency quorum reductions, the global witness
hierarchy, epoch-wide publication gates and expanding retry reservations must
not be inferred as current requirements from these sources. The
[experiment history](../../workbench/spikes/orbital-scenarios/HISTORY.md) gives
Git recovery points for retired models; retained evidence bundles have their own
recovery instructions.
