# Workbench

A place for science and a Swiss army knife for anyone working on SixDB.
Design develops here first, through investigation, experiments, and spikes.
Successor to Calico's `workbench`.

| Path | Purpose |
| --- | --- |
| [spikes/](spikes/README.md) | Investigations, each with its question, reading, code, and evidence together |
| [notebook/](notebook/ideas.md) | Loose thoughts, questions, references, and connections worth returning to |
| [design/](design/README.md) | Cross-cutting direction, design, and conventions |
| [benchmarks/](benchmarks/README.md) | Durable shared workloads and measurement machinery |
| [tools/](tools/README.md) | Scripts, provisioning, and artifact tooling |
| [datasets/](datasets/README.md) | Dataset references, preparation, and generators |

Start with the [aggregate-maintenance closeout](spikes/aggregate-maintenance/CONCLUSIONS.md),
the separate [sketches, filters, and histograms note](notebook/secondary-summaries.md),
or the [Calico overview](notebook/calico.md).

Capture enough to recover an interesting thought. Browse relevant notebook
entries when choosing a question or changing direction; link experimental
surprises to ideas they change or revive. Notes can remain notes indefinitely
and can inform several spikes. A spike can begin with one README and keeps
its home through exploration, adoption, or abandonment.

Investigation-specific reading, runners, benchmarks, and findings belong in
that spike. Shared tools and datasets stay shared. Selected samples, counters,
and provenance live beside findings; full bundles belong in ignored output or
S3. The [retention command](tools/artifacts.md) uploads and verifies a bundle
before writing its compact Git evidence. The [research-experience notes](design/research-experience.md) record what
we learn about using this arrangement. There are no required forms, statuses,
promotion stages, or review ceremonies.
