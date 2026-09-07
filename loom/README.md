# Loom

Binds Orbital to SixDB proper. Schedules tasks, routes them to the right
threads, and interleaves work while requested bytes arrive from disk, network,
DRAM, or other sources. Owns the buffer pool.

Successor to Calico's `loom` and `arbor`. [Orbital](../orbital/README.md) owns
thread spawning, transport, and durability; [Engine](../engine/README.md) owns
the database core.

Task, byte-access, and buffer-pool interfaces will develop through
[Workbench](../workbench/README.md).
