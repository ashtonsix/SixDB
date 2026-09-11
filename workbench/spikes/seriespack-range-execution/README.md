# Executing arbitrary packed-array ranges

How can ordinary bound decoding handle short, clipped and gapped ranges without
paying for a general expression engine on every call? This study owns traversal,
admission, edge handling, output stores and alignment. Native expression
[composition semantics](../ikea-composition/native-regions/README.md) are a separate question.

Start with [ordinary integration](integration.md), the
[clipped-stripe findings](striped-fragments.md), and the final
[ordinary-store follow-up](ordinary-stores.md). The store candidate is promising
on the measured GNR cases and remains uninstalled. Short-call losses and other
hosts' alignment sensitivities still matter.

Earlier discriminators cover [runtime ranges](runtime.md),
[Granite Rapids](granite-rapids.md), [Neoverse V2](neoverse-v2.md),
[admission](admission.md), [expression materialization](expressions.md),
[edges](edges.md), [stores](stores.md), [short stores](short-stores.md) and
[alignment](alignment.md). These record observations under their captured source,
not current cross-machine performance promises.

[regions.h](regions.h) and [prototype](prototype/README.md) retain experimental
mechanisms. [store-candidate](store-candidate/README.md) retains the exact-input
replay of the uninstalled store change. Recurring runtime and boundary workloads
now run through the [benchmark suite](../../benchmarks/seriespack/README.md).
Full historical sweeps and retired wrappers remain
[recoverable](../ikea-composition/archive/validation-20260911.md).
