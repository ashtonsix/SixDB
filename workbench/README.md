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
| Run a script on a cloud machine | [Workers](tools/workers.md): configurable Spot/On-Demand capacity, S3 results, and cleanup |
| Reuse or prepare input data | [Datasets](datasets/README.md): catalog, cached recipes, and spike-local adapters |
| Choose measurement machinery | [Benchmarks](benchmarks/README.md): CPU affinity, sequential repetitions, and non-timing studies |
| Keep selected evidence or recover a run | [Retention and recovery](tools/artifacts.md): compact Git evidence, full S3 bundles, and shared inputs |
| Find current direction and conventions | [Design](design/README.md): cross-cutting notes |

The [spike catalog](spikes/README.md) is the place to browse investigations.
Each study's entry page connects its question to the relevant findings and
follow-ups. The [Calico overview](notebook/calico.md) maps earlier work.

A useful pattern is a short pointer when evidence moves on: “This note records
the first probe; [the follow-up](spikes/aggregate-maintenance/dirty-buffer/README.md)
tests the cheaper dirty-marking idea.” Older reasoning stays readable, and a
new reader can follow what changed. Catalog entries can describe the question;
the study holds its evolving answer.

Shared helpers can be used independently as an investigation needs them. The
[tools index](tools/README.md) has commands and runnable examples;
[research-experience notes](design/research-experience.md) record how those
affordances developed. Agent working defaults are in [AGENTS.md](../AGENTS.md).
