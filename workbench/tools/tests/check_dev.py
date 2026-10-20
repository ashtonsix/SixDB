#!/usr/bin/env python3
"""Exercise spike/benchmark compilation databases in a disposable project."""

import json
import re
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
        # Other Workbench modules are configured too; keep their bodies out of
        # this editor fixture without duplicating the module inventory.
        for module in re.findall(r'add_subdirectory\(([^\s)]+)', (ROOT / 'workbench/CMakeLists.txt').read_text()):
            if module in {'spikes', 'benchmarks'}:
                continue
            (root / "workbench" / module).mkdir(parents=True)
            (root / "workbench" / module / "CMakeLists.txt").write_text("# empty fixture module\n")
        for file in ["workbench/CMakeLists.txt", "workbench/spikes/CMakeLists.txt", "workbench/benchmarks/CMakeLists.txt", "workbench/tools/dev.py", "workbench/tools/dev_watch.py"]:
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
        mirror = root / 'compile_commands.json'
        assert mirror.read_bytes() == (root / 'build/clang/dev/compile_commands.json').read_bytes()
        unchanged = mirror.stat().st_mtime_ns
        dev()
        assert mirror.stat().st_mtime_ns == unchanged
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

        # Initial discovery leaves old, inactive (and even broken) studies alone.
        # Subsequent creation activates targets without --add or a build.
        dev('--sync')
        automatic = root / 'workbench/spikes/automatic'
        (automatic / 'include').mkdir(parents=True)
        (automatic / 'include/editor_fixture.h').write_text('inline constexpr int editor_value = 19;\n')
        (automatic / 'probe.cpp').write_text(
            '#include "editor_fixture.h"\nstatic_assert(EDITOR_FIXTURE == 19);\n'
            'int editor_probe() { return editor_value; }\n')
        automatic_cmake = (
            'add_library(automatic STATIC probe.cpp)\nsixdb_target(automatic)\n'
            'target_include_directories(automatic PRIVATE include)\n'
            'target_compile_definitions(automatic PRIVATE EDITOR_FIXTURE=19)\n')
        (automatic / 'CMakeLists.txt').write_text(automatic_cmake)
        dev('--sync')
        database = root / 'build/clang/dev/compile_commands.json'
        cache = root / 'build/clang/dev/CMakeCache.txt'
        rows = json.loads(database.read_text())
        command = next(row['command'] for row in rows if row['file'] == str(automatic / 'probe.cpp'))
        assert '-DEDITOR_FIXTURE=19' in command and str(automatic / 'include') in command
        parsed = subprocess.run(['clangd-21', '--check=' + str(automatic / 'probe.cpp'), '--tweaks='],
                                cwd=root, capture_output=True, text=True)
        assert parsed.returncode == 0, parsed.stderr
        # Manual failure must also restore the selection and known working bytes.
        before = database.read_bytes(), cache.read_bytes(), mirror.read_bytes()
        failed = subprocess.run([sys.executable, str(root / 'workbench/tools/dev.py'), '--add', 'broken'],
                                cwd=root, capture_output=True, text=True)
        assert failed.returncode and (database.read_bytes(), cache.read_bytes(), mirror.read_bytes()) == before

        # An incomplete new study cannot block a different valid new study.
        incomplete = root / 'workbench/spikes/incomplete'
        incomplete.mkdir()
        (incomplete / 'CMakeLists.txt').write_text('add_library(incomplete STATIC not-yet.cpp)\n')
        later = root / 'workbench/spikes/later'
        later.mkdir()
        (later / 'CMakeLists.txt').write_text('add_library(later STATIC probe.cpp)\n')
        (later / 'probe.cpp').write_text('int later_probe;\n')
        failed = subprocess.run([sys.executable, str(root / 'workbench/tools/dev.py'), '--sync'],
                                cwd=root, capture_output=True, text=True)
        assert failed.returncode
        assert any(row['file'] == str(later / 'probe.cpp') for row in json.loads(database.read_text()))
        assert 'incomplete' not in dev('--list').splitlines()
        (incomplete / 'not-yet.cpp').write_text('int finished_later;\n')
        dev('--sync')  # Retry even though CMake itself has not changed.
        assert 'incomplete' in dev('--list').splitlines()
        (automatic / 'CMakeLists.txt').write_text(automatic_cmake.replace('=19', '=23'))
        dev('--sync')
        assert any('-DEDITOR_FIXTURE=23' in row['command'] for row in json.loads(database.read_text()))
        renamed = automatic.with_name('renamed')
        automatic.rename(renamed)
        dev('--sync')
        assert 'automatic' not in dev('--list').splitlines() and 'renamed' in dev('--list').splitlines()
        assert any(row['file'] == str(renamed / 'probe.cpp') for row in json.loads(database.read_text()))
        shutil.rmtree(renamed)
        dev('--sync')
        assert not any('renamed' in row['file'] for row in json.loads(database.read_text()))
        # Retiring an active benchmark must not leave an invalid cached suite
        # blocking refresh of the remaining spikes and their IntelliSense.
        dev('--add-benchmark', 'recurring')
        (root / 'workbench/benchmarks/recurring/CMakeLists.txt').unlink()
        dev('--sync')
        assert 'benchmark:recurring' not in dev('--list').splitlines()
        assert not any('/benchmarks/recurring/' in row['file'] for row in json.loads(database.read_text()))
        assert any(row['file'] == str(later / 'probe.cpp') for row in json.loads(database.read_text()))
        globbed = root / 'workbench/spikes/globbed'
        globbed.mkdir()
        (globbed / 'CMakeLists.txt').write_text(
            'file(GLOB sources *.cpp)\nif(sources)\nadd_library(globbed STATIC ${sources})\nendif()\n')
        dev('--sync')
        (globbed / 'arrived-after-configure.cpp').write_text('int globbed_source;\n')
        dev('--sync')
        assert any(row['file'] == str(globbed / 'arrived-after-configure.cpp')
                   for row in json.loads(database.read_text()))
        assert not list((root / 'build').rglob('*.o'))
        print("PASS: independent spike/benchmark activation, removal, target include paths/definitions, C++23/tuning flags, "
              "inactive broken-study isolation, empty-database cleanup, configure-only operation, "
              "Ikea header isolation, automatic create/edit/rename/removal, failed-config rollback, "
              "and recovery from incomplete new spikes without blocking valid additions.")


if __name__ == "__main__":
    main()
