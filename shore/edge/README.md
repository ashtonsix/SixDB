# Edge

Shore owns Edge, a separately deployable program connecting clients and SixDB
replicas across networks and regions. Candidate deployment environments include
Cloudflare Workers, NetActuate and colocation in carrier hotels. Runtime,
packaging and platform-specific capabilities remain open.

## Candidate responsibilities

- Client gateway and connection multiplexing, sharing physical connections while
  retaining the state, progress and backpressure of individual operations.
- Forwarding work to an eligible replica holding representations specialized for
  the query type. Connection placement and per-query routing may make different
  choices.
- Broadcast for replication across many regions, using shared paths and regional
  fanout where they reduce network cost and meet delivery obligations.

These are responsibilities to investigate, not implemented features or a selected
topology. Edge can develop alongside [Shore's frontends and
interfaces](../README.md); its separate deployment does not require a full
database replica at every gateway or relay.

## Composition seams

[Engine](../../engine/README.md) defines accepted IRs, query meaning and the
execution capabilities to which Shore adapts. [Orbital](../../orbital/README.md)
owns durable objects, transactions and recovery. Edge routing needs information
about replica capabilities, representation availability and eligibility for the
requested version and operation, preserving the identity, version and
interpretation of selected representations. The source and lifetime of that
information remain to be worked out with those owners.

Forwarding and relay receipt must preserve the database's completion and durability
meanings. A representation suited to a query does not by itself establish that a
replica can serve the requested version; a relay's receipt does not establish
durable replication. Shared transport also needs to account for slow consumers,
cancellation and retained buffers per operation.

## Research context

The [dissemination survey](../../workbench/notebook/dissemination/README.md)
retains the prior exploration of networking, placement, representation and
delivery. Its [networking catalog](../../workbench/notebook/dissemination/SOURCE-SURVEY-NETWORK.md)
includes shared regional paths and input origin (N01–N02), multiplexing and
per-recipient obligations (N14–N15), connection steering (N46), and the separation
of routing from authority (N80). The [workload](../../workbench/notebook/dissemination/SOURCE-SURVEY-WORKLOADS.md)
and [delivery](../../workbench/notebook/dissemination/SOURCE-SURVEY-DELIVERY.md)
catalogs cover representation-aware serving and completion/recovery duties.

The [archived edge study](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dissemination/EDGE.md)
compared edge-origin and cloud-origin broadcast, regional relays and return
traffic. Savings depended on where bytes already existed, charged boundaries,
reply size and relay capacity. It used synthetic tariffs and modeled traffic;
it establishes neither current provider pricing nor a deployment choice.
The [retirement guide](../../workbench/notebook/retired-spikes.md) owns recovery
of that study. These findings inform Edge without selecting a protocol or
inheriting an old topology.
