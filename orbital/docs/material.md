# Supplying material

Orbital publishes caller-supplied material. The caller defines its format,
geometry, validation, cloning and conversion; Orbital owns catalog and physical
lifetimes. A storage adapter and its concrete instantiation TU stay with that
caller. The [caller](../test/extension.cpp) and its
[material TU](../test/extension_material.cpp) use plain integer data.

## Compile once

Ordinary caller headers include `orbital/object.h` and, for native physical
storage, `orbital/representation.h`. One implementation TU includes the needed
opt-ins:

```cpp
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
```

It supplies `Physical` / `Control` and explicitly instantiates their cold
operations. Link `orbital::orbital`; no source-tree include path is needed.
The opt-ins compile against the same Orbital headers/library as the caller.
Ordinary read TUs do not include these definitions. The exact Control operations
and selected-proof obligations are beside the
[Representation definitions](../include/orbital/instantiate/representation.h).
