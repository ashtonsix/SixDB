#!/usr/bin/env python3
"""Exercise active-spike compilation databases in a disposable project."""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from dev import checkout_root

ROOT = Path(__file__).resolve().parents[2]


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
        for module in ["ikea", "orbital", "loom", "engine", "shore"]:
            (root / module).mkdir()
            (root / module / "CMakeLists.txt").write_text("# empty fixture module\n")
        (root / "workbench/tools").mkdir(parents=True)
        (root / "workbench/spikes").mkdir()
        for file in ["workbench/CMakeLists.txt", "workbench/spikes/CMakeLists.txt", "workbench/tools/dev.py"]:
            shutil.copy2(ROOT / file, root / file)
        for name in ["first", "second"]:
            path = root / "workbench/spikes" / name
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
                assert str(root / "workbench/spikes" / study / "include") in row["command"]
                assert "-std=c++23" in row["command"] and "SIXDB_TUNE_GENERIC=1" in row["command"]
                result[study] = row
            return result

        dev("--add", "first")
        assert set(entries()) == {"first"}
        first = entries()["first"]
        dev("--add", "second")
        assert set(entries()) == {"first", "second"} and entries()["first"] == first
        assert dev("--list").splitlines() == ["first", "second"]
        dev("--remove", "first")
        assert set(entries()) == {"second"}
        dev("--remove", "second")
        assert entries() == {}
        # The real configure steps must not have built either source target.
        assert not list((root / "build/clang/dev/workbench/spikes").rglob("*.o"))
        print("PASS: additive activation, removal, target include paths/definitions, C++23/tuning flags, "
              "inactive broken-study isolation, empty-database cleanup, and configure-only operation.")


if __name__ == "__main__":
    main()
