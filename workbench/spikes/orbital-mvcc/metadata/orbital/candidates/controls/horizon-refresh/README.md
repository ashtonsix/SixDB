# Refresh retention between Objects

[`horizon-refresh.patch`](horizon-refresh.patch) is an unapplied control against
`3fabe9e34f4f81886e62fee1a025e950b473e3e9`. Full maintenance remains the default;
this does not apply the separate bounded32 caller policy.

The original bounded sweep keeps one reader-cut snapshot and protected suffix
boundary across its entire debt traversal. Continued publication lengthens that
protected suffix; a reader that already closed can therefore prevent useful
retirement for the whole first sweep.

This control separates starting a sweep from refreshing its retention plan.
After completing an Object, changed cuts or retention boundaries cause a new
plan before the next Object. The existing chronological cut sampler is reused;
new historical readers are sampled before the boundary advances. Debt position,
remaining Object count and original sweep generation survive the refresh. No
field, queue or caller policy is added. `plan_work` now also counts these
between-Object refresh attempts; the existing `last_sweep_*` fields describe the
latest completed plan.

A plan interrupted by reader changes still processes one Object before another
refresh, so repeated admission/publication cannot continuously restart sampling
at the same Object boundary. Its sampled suffix remains conservative. Existing
callback restart handling and accepted-work pins are unchanged. Planning
allocation refusal retains the traversal position and retries the same plan.

The added deterministic history publishes 4,099 replacements across 64 Objects
in 65 groups, rotates a coherent reader every eight groups, and gives 32 metadata
steps to each publication and rotation. It verifies every Object against values
recorded at reader enrollment after subsequent writes; simply advancing the
boundary without admitting new reader cuts would violate that oracle.

| Same 2,336 metadata steps | Original | Control |
| --- | ---: | ---: |
| Versions retained | 4,163 | 2,417 |
| Versions reclaimed | 0 | 1,746 |
| Completed Objects | 58 | 88 |
| Completed sweeps | 0 | 1 |
| Plan attempts | 1 | 58 |
| Callback restarts | 0 | 0 |

The original code fails the new forward-reclamation assertion with exactly the
measured zero-reclamation counters. The control passes that history and all
nine existing bounded histories across 14 Store profiles, including coherent
and late-discovered reads, absence/delete/recreate, accepted work, refusal,
interrupted plan construction, callback mutation and continuous publication.
Final settlement leaves exactly 64 current versions and all owned memory is
released on teardown.

Qualification used pinned Linux AArch64 Clang 21.1.8, Release with assertions,
`-j2`, in the private incremental projection. The patch applies cleanly to its
named commit; exact source hashes, commands and logs are in ignored
`build/orbital-mvcc/metadata-projected-integration/horizon-refresh-receipt.json`.
No timings or sanitizer campaign were run. This fixes conservative-plan lag,
not the service deficit: 2,336 steps cannot retire 4,096 obsolete versions even
before planning and traversal costs. Bounded maintenance still needs sufficient
service for the publication rate.
