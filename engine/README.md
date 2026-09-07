# Engine

The database core: the main SQL query interface, planning, execution, and core
data structures. Multi-shard transactions are in scope.

Successor to Calico's `engine` and `foyer`. [Shore](../shore/README.md) provides
external interfaces and integrations; [Loom](../loom/README.md) supplies task
scheduling and the binding to Orbital.

Detailed SQL semantics, representations, and execution mechanisms will develop
through [Workbench](../workbench/README.md).
