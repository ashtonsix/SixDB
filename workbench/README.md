# Workbench

A place for science and a Swiss army knife for anyone working on SixDB.
Design develops here first, through investigation, experiments, and spikes.
Successor to Calico's `workbench`.

| To… | Start here |
| --- | --- |
| Capture or revisit an idea | [Notebook](notebook/ideas.md): loose thoughts, questions, and connections |
| Start or explore an investigation | [Spikes](spikes/README.md): one home for its question, reading, code, and findings |
| Build a prototype or enable editor support | [Study builds](spikes/README.md#building-a-study): opt-in targets and independent TU compilation |
| Run while continuing to edit | [Run helpers](tools/README.md): captured sources, incremental workspaces, and receipts; existing runners are examples |
| Reuse or prepare input data | [Datasets](datasets/README.md): catalog, cached recipes, and spike-local adapters |
| Choose measurement machinery | [Benchmarks](benchmarks/README.md): CPU affinity, sequential repetitions, and non-timing studies |
| Keep selected evidence or recover a run | [Retention and recovery](tools/artifacts.md): compact Git evidence, full S3 bundles, and shared inputs |
| Find current direction and conventions | [Design](design/README.md): cross-cutting notes |

Start with the [regexp-lowering closeout](spikes/regexp-lowering/CONCLUSIONS.md),
the [trie-remapping closeout](spikes/trie-remapping/README.md),
the [aggregate-maintenance closeout](spikes/aggregate-maintenance/CONCLUSIONS.md),
the [row-signature findings](spikes/row-filter-signatures/FINDINGS.md),
the separate [sketches, filters, and histograms note](notebook/secondary-summaries.md),
or the [Calico overview](notebook/calico.md).

The [spike guide](spikes/README.md) describes an investigation's home;
the [notebook](notebook/ideas.md) holds thoughts that can inform several studies.
Shared helpers can be used independently as an investigation needs them.
Agent working defaults are in [AGENTS.md](../AGENTS.md).
The [research-experience notes](design/research-experience.md#shared-inputs-and-fewer-chores)
explain the recent changes; the [tools index](tools/README.md) lists the scripts
and checks.
