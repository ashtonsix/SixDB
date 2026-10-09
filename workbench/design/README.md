# Design development

Cross-cutting conventions and current design recommendations live here. Module
contracts belong with their module; experimental detail stays with its
[investigation](../spikes/README.md).
Most architectural bets and constraints have yet to be recorded.

- [Build and code conventions](conventions.md): current settings and open choices.
- [Tuning flags](tuning.md): microarchitecture policy independent of ISA availability.
- [Orbital/Loom design](orbital/DESIGN.md): cross-module structure and future protocol direction; the [runtime guide](../../orbital/README.md) owns Stage 1 interfaces. The withdrawn Stage 2 baseline belongs to the [MVCC investigation](../spikes/orbital-mvcc/README.md).
- [Project marks and visual direction](identity/README.md): intent, motion, assets and supporting material.

[Project direction](../../README.md) owns the overall intent. Historical context
lives in the notebook: [Calico build lessons](../notebook/build-iteration.md) and
[the first research loops](../notebook/research-experience.md).
