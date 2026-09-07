# Orbital

The OS-like layer underneath SixDB. Owns data durability, network transport,
thread spawning, and related system services.

Successor to Calico's `xmem` and `omachine`. [Loom](../loom/README.md) binds
Orbital to SixDB proper and owns database task scheduling and the buffer pool.

Service interfaces and operating modes will develop through
[Workbench](../workbench/README.md).
