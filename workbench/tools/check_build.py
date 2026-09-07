#!/usr/bin/env python3
"""Check real build rules on a disposable multi-TU fixture; no production build."""

import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args, success=True):
    result = subprocess.run([str(a) for a in args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (result.returncode == 0) != success:
        raise RuntimeError(f"Command: {args}\n{result.stdout}")
    return result.stdout


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_tuning(source, directory):
    """Compile header-free probes for both backends; no target executables run."""
    study = source / "workbench/spikes/tuning"
    study.mkdir()
    (study / "CMakeLists.txt").write_text('''
add_library(tuning_probe OBJECT probe.cpp)
sixdb_target(tuning_probe)
''')
    names = ("SIXDB_TUNE_GENERIC", "SIXDB_TUNE_GRANITE_RAPIDS",
             "SIXDB_TUNE_ZEN5", "SIXDB_TUNE_NEOVERSE_V2")
    (study / "probe.cpp").write_text(
        "\n".join(f"#ifndef {name}\n#error Missing {name}\n#endif" for name in names)
        + "\nstatic_assert(" + " + ".join(names) + " == 1);\n"
        + 'extern "C" int probe(int x) { return x + 1; }\n')

    def configure(triple, march, tune, success=True):
        binary = Path(directory) / f"{triple}-{march}-{tune}"
        output = run("cmake", "-S", source, "-B", binary, "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=Release", "-DSIXDB_SPIKES=tuning",
                     f"-DCMAKE_CXX_COMPILER_TARGET={triple}",
                     "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                     f"-DSIXDB_MARCH={march}", f"-DSIXDB_TUNE={tune}", success=success)
        return binary, output

    def macros(triple, march, tune):
        binary, _ = configure(triple, march, tune)
        run("cmake", "--build", binary, "--target", "tuning_probe")
        command, = json.loads((binary / "compile_commands.json").read_text())
        # Reuse the actual target's compiler flags to inspect its preprocessor.
        args = iter(shlex.split(command["command"]))
        preprocess = []
        for arg in args:
            if arg == "-o":
                next(args)
            elif arg != "-c":
                preprocess.append(arg)
        lines = run(*preprocess, "-dM", "-E").splitlines()
        defines = {parts[1]: parts[2] if len(parts) > 2 else ""
                   for line in lines if line.startswith("#define ")
                   for parts in [line.split(maxsplit=2)]}
        expected = names[("generic", "granite-rapids", "zen5", "neoverse-v2").index(tune)]
        require(all(defines[name] == str(int(name == expected)) for name in names),
                f"Incorrect tuning definitions for {tune}")
        ir = run(*preprocess, "-S", "-emit-llvm", "-o", "-")
        cpu = {"generic": "generic", "granite-rapids": "graniterapids",
               "zen5": "znver5", "neoverse-v2": "neoverse-v2"}[tune]
        require(f'"tune-cpu"="{cpu}"' in ir, "Compiler did not apply the requested tuning")
        return {name: value for name, value in defines.items() if name not in names}

    x86 = "x86_64-linux-gnu"
    arm = "aarch64-linux-gnu"
    baseline = macros(x86, "x86-64-v3", "generic")
    for tune in ("granite-rapids", "zen5"):
        require(macros(x86, "x86-64-v3", tune) == baseline,
                f"{tune} tuning changed compiler feature macros")
    require("__AVX512F__" not in baseline, "Unexpected AVX-512 in baseline")
    require("__AVX512F__" in macros(x86, "x86-64-v4", "zen5"),
            "Explicit ISA selection did not enable AVX-512")
    baseline = macros(arm, "armv8-a", "generic")
    require(macros(arm, "armv8-a", "neoverse-v2") == baseline,
            "Neoverse V2 tuning changed compiler feature macros")
    require("__ARM_FEATURE_SVE2" not in baseline, "Unexpected SVE2 in baseline")
    require("__ARM_FEATURE_SVE2" in macros(arm, "armv9-a", "neoverse-v2"),
            "Explicit ISA selection did not enable SVE2")
    for triple, march, tune in ((x86, "x86-64-v3", "neoverse-v2"),
                               (arm, "armv8-a", "zen5")):
        _, message = configure(triple, march, tune, success=False)
        require("unsupported by the configured compiler target" in message,
                "Mismatched target failed for an unrelated reason")


def main():
    with tempfile.TemporaryDirectory(prefix="sixdb-build-") as directory:
        source = Path(directory) / "source"
        source.mkdir()
        shutil.copy2(ROOT / "CMakeLists.txt", source)
        shutil.copytree(ROOT / "cmake", source / "cmake")
        for module in ("ikea", "orbital", "loom", "engine", "shore", "workbench"):
            (source / module).mkdir()
            (source / module / "CMakeLists.txt").write_text("")
        (source / "workbench/CMakeLists.txt").write_text("add_subdirectory(spikes)\n")
        spikes = source / "workbench/spikes"
        spikes.mkdir()
        shutil.copy2(ROOT / "workbench/spikes/CMakeLists.txt", spikes)
        study = spikes / "fixture"
        study.mkdir()
        (study / "CMakeLists.txt").write_text('''
add_library(fixture_core STATIC value.cpp)
sixdb_target(fixture_core)
foreach(name one two)
  add_executable(fixture_${name} ${name}.cpp)
  sixdb_target(fixture_${name})
  target_link_libraries(fixture_${name} PRIVATE fixture_core)
endforeach()
sixdb_release_artifact(fixture_one)
''')
        (study / "value.h").write_text("#pragma once\n#include <expected>\nstd::expected<int, int> value();\n")
        (study / "private.h").write_text("#pragma once\nconstexpr int answer = 42;\n")
        (study / "value.cpp").write_text('#include "value.h"\n#include "private.h"\nstd::expected<int, int> value() { return answer; }\n')
        for name in ("one", "two"):
            (study / f"{name}.cpp").write_text('#include "value.h"\nint main() { return value().has_value() ? 0 : 1; }\n')
        broken = spikes / "unselected"
        broken.mkdir()
        (broken / "CMakeLists.txt").write_text('message(FATAL_ERROR "Unselected spike was configured")\n')

        def configure(binary, config="RelWithDebInfo", *flags):
            return run("cmake", "-S", source, "-B", binary, "-G", "Ninja",
                       f"-DCMAKE_BUILD_TYPE={config}", "-DSIXDB_SPIKES=fixture", *flags)

        binary = Path(directory) / "dev"
        configure(binary)
        base = binary / "workbench/spikes/fixture"
        objects = [base / "CMakeFiles/fixture_core.dir/value.cpp.o"]
        objects += [base / f"CMakeFiles/fixture_{name}.dir/{name}.cpp.o" for name in ("one", "two")]

        def build(*targets):
            return run("cmake", "--build", binary, "--target", *targets)

        def stamps():
            return [p.stat().st_mtime_ns for p in objects]

        run("cmake", "--build", binary)
        require(not any(p.exists() for p in objects), "Default build compiled a spike")
        build("fixture_one")
        require(not objects[2].exists(), "Focused build compiled another executable")
        shared = objects[0].stat().st_mtime_ns
        build("fixture_two")
        require(objects[0].stat().st_mtime_ns == shared, "Shared library was recompiled for another consumer")
        before = stamps()
        build("fixture_one", "fixture_two")
        require(stamps() == before, "No-op build changed objects")

        # Compile a changed TU without relinking either consumer.
        executable = base / "fixture_one"
        linked = executable.stat().st_mtime_ns
        with (study / "value.cpp").open("a") as stream:
            stream.write("\n// Source invalidation probe.\n")
        build(objects[0].relative_to(binary))
        after = stamps()
        require(after[0] != before[0] and after[1:] == before[1:], "Source change rebuilt unrelated TUs")
        require(executable.stat().st_mtime_ns == linked, "Object-only build linked the executable")
        build("fixture_one", "fixture_two")
        run(executable)

        before = stamps()
        with (study / "private.h").open("a") as stream:
            stream.write("\n// Private dependency probe.\n")
        build("fixture_one", "fixture_two")
        after = stamps()
        require(after[0] != before[0] and after[1:] == before[1:], "Private header invalidation was not isolated")
        for change in ("header", "flags"):
            before = stamps()
            if change == "header":
                with (study / "value.h").open("a") as stream:
                    stream.write("\n// Public dependency probe.\n")
            else:
                configure(binary, "RelWithDebInfo", "-DCMAKE_CXX_FLAGS=-DSIXDB_BUILD_PROBE=1")
            build("fixture_one", "fixture_two")
            require(all(a != b for a, b in zip(before, stamps())), f"{change} failed to invalidate dependent objects")
        before = stamps()
        build("fixture_one", "fixture_two")
        require(stamps() == before, "Build after invalidation did not settle")

        commands = json.loads((binary / "compile_commands.json").read_text())
        require(len(commands) == 3, "Sources were duplicated or combined")
        for item in commands:
            for flag in ("-std=c++23", "-O2", "-ffp-contract=off", "-fno-fast-math"):
                require(flag in item["command"], f"Missing {flag}")
            require("-DNDEBUG" not in item["command"], "Development disabled assertions")

        release = Path(directory) / "release"
        configure(release, "Release")
        run("cmake", "--build", release, "--target", "fixture_one_dist")
        packed = release / "dist/bin/fixture_one"
        debug = release / "dist/symbols/fixture_one.debug"
        original = release / "workbench/spikes/fixture/fixture_one"
        run(packed)
        require(packed.stat().st_size < original.stat().st_size, "Stripping did not reduce artifact size")
        sections = run("readelf", "-SW", packed)
        require(".symtab" not in sections and ".debug_info" not in sections, "Distribution artifact retains private symbols")
        require(".gnu_debuglink" in sections, "Distribution artifact lacks its debug link")
        require(".debug_info" in run("readelf", "-SW", debug), "Separate debug information is missing")
        packed_stamp = packed.stat().st_mtime_ns
        run("cmake", "--build", release, "--target", "fixture_one_dist")
        require(packed.stat().st_mtime_ns == packed_stamp, "No-op build repackaged the executable")
        # Missing sidecars are regenerated, not silently treated as complete.
        debug.unlink()
        run("cmake", "--build", release, "--target", "fixture_one_dist")
        require(debug.is_file(), "Missing debug sidecar was not regenerated")
        if shutil.which("g++"):
            rejected = run("cmake", "-S", source, "-B", Path(directory) / "wrong-compiler",
                           "-G", "Ninja", "-DCMAKE_CXX_COMPILER=g++", success=False)
            require("SixDB requires Clang" in rejected, "Wrong compiler failed for an unrelated reason")
        check_tuning(source, directory)
        print("PASS: spike isolation, shared objects, compile-only builds, source/header/flag invalidation, compiler pin, stripped release artifacts, and tuning/ISA separation")


if __name__ == "__main__":
    main()
