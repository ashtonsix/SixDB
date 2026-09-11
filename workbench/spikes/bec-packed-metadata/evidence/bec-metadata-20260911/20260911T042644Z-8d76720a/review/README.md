# Independent BEC metadata review

The audit checks both source archives, every compiled source, linked object/archive member, all recorded artifact hashes, the global target screen, and independent Python dataset/query oracles. Local quality source hashes were matched after the checks.

Code sizes cover only the nine adapter endpoints in the measured archive. Calls and vector stack accesses count static instruction sites, not dynamic events. The materialized path contains a bound decoder call and a conditional final-tail memset at each refill site. Shared library code is excluded from endpoint sizes. Only standalone refill assembly is selected here; whole-count disassembly is recoverable from the retained measured archive.
