# Investigating compilation cost

Start with one costly translation unit. `compile_probe.py` selects its existing
Clang command from `compile_commands.json` and runs it serially into a new output
directory. Optimization, debug and ISA flags stay as configured; original build
objects, dependencies and Ninja logs remain available to other work.

From a configured checkout on Linux:

```sh
python3 workbench/tools/compile_probe.py build/clang/dev \
  --source ikea/src/seriespack/view.cpp \
  --source ikea/src/seriespack/operations.cpp \
  --output build/compile-probes/readers
```

`--plan` prints the commands without creating outputs. Source selectors are exact
paths or unique suffixes; `--object` disambiguates a source compiled into several
targets. Sources run in the listed order, followed by any object selectors.
The helper stops on failure and returns nonzero. It supports ordinary direct
Clang object commands, without compiler launchers, response files or module builds.

On a worker, put the output beneath `$SIXDB_RESULTS` so the normal upload retains
it. To compare a TU with two smaller replacements, run the original once and the
two replacements serially, each from its own configured captured checkout. Match
compiler/flags, host and affinity; repeat if variability matters. No linking or
tests are included. OS caches are left warm, and TU hashes alone do not identify
included headers: use the existing [capture workflow](README.md#captured-experiment-runs).

`summary.json` keeps commands, compiler versions, source identities, per-compile
wall/user/system seconds and process-max RSS in KiB. Serial compile totals exclude
helper bookkeeping; elapsed time includes it. A TU split can reduce peak memory
while increasing total work. RSS is the largest process footprint reported by
Linux [`wait4`/rusage](https://man7.org/linux/man-pages/man2/getrusage.2.html),
not the sum of concurrent process memory. Objects, logs and optional
traces sit beside the compact summary; retain bulky outputs through the existing
[artifact tools](artifacts.md) when useful. Redirected outputs can change debug
paths/object bytes; they are diagnostic objects, not measured runtime binaries.

For attribution, add `--time-trace`. It records this command change and puts each
[Clang trace](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-ftime-trace)
beside its object. Tracing adds overhead and does not profile memory. The existing
`SIXDB_TIME_TRACE=ON` CMake option enables it for a whole diagnostic configuration.

Failure receipts include return status/signal and available kernel OOM messages
and inherited [cgroup event counters](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory).
Those scopes can include other tasks. Only a new kernel record naming the exact
compiler PID sets `compiler_pid_confirmed`; a killed driver child may not match.
A cgroup OOM increment or an unexplained SIGKILL is context, not that confirmation.
Unavailable records remain unavailable; the helper needs no new privileges and
does not reset counters or alter resource limits.
