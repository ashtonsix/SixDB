#!/usr/bin/env python3
"""Exercise spike/benchmark compilation databases in a disposable project."""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from dev import checkout_root

ROOT = Path(__file__).resolve().parents[3]


def main():
    with tempfile.TemporaryDirectory(prefix="sixdb-dev-check-") as directory:
        root = Path(directory)
        # Home aliases must not leak into the editor database. Projects outside
        # that home remain untouched; symlinks and spaces are legitimate paths.
        home = root / 'native home'
        home.mkdir()
        project = home / 'project'
        project.mkdir()
        alias = root / 'home alias'
        alias.symlink_to(home, target_is_directory=True)
        assert checkout_root(alias / 'project', home) == project
        assert checkout_root(root, home) == root.resolve()
        for file in ["CMakeLists.txt", "CMakePresets.json"]:
            shutil.copy2(ROOT / file, root / file)
        shutil.copytree(ROOT / "cmake", root / "cmake")
        # Stub module bodies without maintaining a second module inventory.
        for module in sorted(p.parent.name for p in ROOT.glob('*/CMakeLists.txt')
                             if p.parent.name != 'workbench'):
            (root / module).mkdir()
            (root / module / "CMakeLists.txt").write_text("# empty fixture module\n")
        (root / "workbench/tools").mkdir(parents=True)
        (root / "workbench/spikes").mkdir()
        (root / "workbench/benchmarks").mkdir()
        for file in ["workbench/CMakeLists.txt", "workbench/spikes/CMakeLists.txt", "workbench/benchmarks/CMakeLists.txt", "workbench/tools/dev.py"]:
            shutil.copy2(ROOT / file, root / file)
        for kind, name in [("spikes", "first"), ("spikes", "second"), ("benchmarks", "recurring")]:
            path = root / "workbench" / kind / name
            (path / "include").mkdir(parents=True)
            (path / "include/fixture.h").write_text("inline constexpr int fixture_value = 7;\n")
            (path / "probe.cpp").write_text('#include "fixture.h"\nint probe() { return fixture_value; }\n')
            (path / "CMakeLists.txt").write_text(
                f"add_library({name} STATIC probe.cpp)\nsixdb_target({name})\n"
                f"target_include_directories({name} PRIVATE include)\n"
                f"target_compile_definitions({name} PRIVATE FIXTURE_{name}=1)\n")
        broken = root / "workbench/spikes/broken"
        broken.mkdir()
        (broken / "CMakeLists.txt").write_text('message(FATAL_ERROR "inactive study was configured")\n')

        def dev(*args):
            result = subprocess.run([sys.executable, str(root / "workbench/tools/dev.py"), *args],
                cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(result.stdout)
            return result.stdout.strip()

        def entries():
            data = json.loads((root / "build/clang/dev/compile_commands.json").read_text())
            result = {}
            for row in data:
                study = Path(row["file"]).parent.name
                assert f"-DFIXTURE_{study}=1" in row["command"]
                assert str(Path(row["file"]).parent / "include") in row["command"]
                assert "-std=c++23" in row["command"] and "SIXDB_TUNE_GENERIC=1" in row["command"]
                result[study] = row
            return result

        dev("--add", "first")
        assert set(entries()) == {"first"}
        first = entries()["first"]
        dev("--add", "second")
        assert set(entries()) == {"first", "second"} and entries()["first"] == first
        assert dev("--list").splitlines() == ["first", "second"]
        dev("--add-benchmark", "recurring")
        assert set(entries()) == {"first", "second", "recurring"}
        assert dev("--list").splitlines() == ["first", "second", "benchmark:recurring"]
        dev("--remove", "first")
        assert set(entries()) == {"second", "recurring"}
        dev("--add", "first")
        assert set(entries()) == {"first", "second", "recurring"}
        dev()
        assert set(entries()) == {"first", "second", "recurring"}
        dev("--remove-benchmark", "recurring")
        assert set(entries()) == {"first", "second"}
        dev("--remove", "first")
        assert set(entries()) == {"second"}
        dev("--remove", "second")
        assert entries() == {}
        # The real configure steps must not have built either source target.
        assert not list((root / "build/clang/dev/workbench/spikes").rglob("*.o"))
        # Reproduce a header borrowing an unrelated spike's same-named TU.
        ikea = root / 'ikea'
        (ikea / 'include/ikea/tuplepack/author').mkdir(parents=True)
        (ikea / 'src').mkdir()
        (ikea / 'include/ikea/value.h').write_text('inline constexpr int value = 7;\n')
        header = ikea / 'include/ikea/tuplepack/author/native.h'
        header.write_text('#include <ikea/value.h>\ninline int read() { return value; }\n')
        (ikea / 'src/anchor.cpp').write_text('#include <ikea/tuplepack/author/native.h>\n')
        (ikea / 'CMakeLists.txt').write_text(
            'add_library(ikea_fixture STATIC src/anchor.cpp)\n'
            'sixdb_target(ikea_fixture)\n'
            'target_include_directories(ikea_fixture PUBLIC include)\n')
        spike = root / 'workbench/spikes/ikea-composition'
        (spike / 'probes/ikea-blocks').mkdir(parents=True)
        (spike / 'probes/ikea-blocks/native.cpp').write_text('int unrelated;\n')
        (spike / 'CMakeLists.txt').write_text(
            'add_library(unrelated STATIC probes/ikea-blocks/native.cpp)\nsixdb_target(unrelated)\n')
        dev('--add', 'ikea-composition')
        (root / '.clangd').write_text('CompileFlags:\n  CompilationDatabase: build/clang/dev\n')

        def check_header():
            return subprocess.run(['clangd-21', '--check=' + str(header), '--tweaks='],
                                  cwd=root, capture_output=True, text=True)

        wrong = check_header()
        assert wrong.returncode and "'ikea/value.h' file not found" in wrong.stderr, wrong.stderr
        shutil.copy2(ROOT / '.clangd', root / '.clangd')
        right = check_header()
        assert right.returncode == 0, right.stderr
        assert 'Compile command inferred from ' + str(ikea / 'src/anchor.cpp') in right.stderr
        # Removing the module must also empty its view, not retain stale flags.
        (ikea / 'CMakeLists.txt').write_text('# empty fixture module\n')
        dev()
        assert json.loads((root / 'build/clang/dev/ikea/compile_commands.json').read_text()) == []
        print("PASS: independent spike/benchmark activation, removal, target include paths/definitions, C++23/tuning flags, "
              "inactive broken-study isolation, empty-database cleanup, configure-only operation, "
              "and Ikea header isolation from same-named spike sources.")


if __name__ == "__main__":
    main()
