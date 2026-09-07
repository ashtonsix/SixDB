# Starting direction

SixDB aims for HTAP dominance with strong ELT, reducing the infrastructure
needed to serve mixed workloads. It is intended to become a production
database. Workloads are broad; no narrow workload or benchmark defines the
project's destination.

Calico pursued a general database and overextended into graph, search, vector,
and document territory. Its transactional and analytical results were strong,
including benchmark wins, but it did not reach production readiness.

Many architectural bets were effective in some contexts and limiting in others.
The universal dual-arity prefix-addressed trie is one example to revisit.
Early choices constrained later work and complicated module seams. SixDB draws
on that evidence while recalibrating the mechanisms and their scope.

C++ is primary. Linux, distribution, AMD/Zen5, and durability come first, with
a wide array of operating modes to support. Multi-shard transactions are in
scope. These are the constraints stated so far, not a complete inventory.

Science and experiments come first. Further bets and constraints will be
introduced as work develops; experiment results will inform the design.

Source: Ashton's initial calibration, 2026-09-07. The Calico performance
description above records project context, not a new benchmark assessment.
