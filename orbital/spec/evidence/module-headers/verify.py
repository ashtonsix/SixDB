#!/usr/bin/env python3
"""Verify this one header-insertion audit against its exact checked sources."""

import hashlib
import json
from pathlib import Path
import re
import subprocess


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SPEC = ROOT / "orbital/spec"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify():
    audit = json.loads((HERE / "audit.json").read_bytes())
    require(audit["schema"] == 1, "unknown audit schema")
    require(audit["transformation"] == "insert-purpose-block-after-module-declaration",
            "unknown transformation")
    base = audit["base_commit"]
    require(re.fullmatch(r"[0-9a-f]{40}", base), "invalid base commit")

    def original(name):
        return subprocess.check_output(
            ["git", "show", f"{base}:orbital/spec/{name}"], cwd=ROOT)

    inputs_bytes = (HERE.parent / "admission-renewal/inputs.json").read_bytes()
    require(sha(inputs_bytes) == audit["evidence_inputs_sha256"], "evidence index changed")
    index = json.loads(inputs_bytes)
    inputs = index["source_sha256"]
    files = audit["files"]
    for name, change in files.items():
        require(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*\.tla", name), "invalid module path")
        old = original(name)
        require(sha(old) == change["before_sha256"] == inputs[name],
                f"{name}: original differs from checked source")
        first, remainder = old.split(b"\n", 1)
        require(re.fullmatch(rb"-+ MODULE " + name[:-4].encode() + rb" -+", first),
                f"{name}: unexpected module declaration")
        header = change["inserted_header"].encode("ascii")
        require(re.fullmatch(rb"\(\* [\x20-\x7e]*(?:\n   [\x20-\x7e]*){0,2} \*\)\n", header),
                f"{name}: insertion is not a short purpose block")
        require(b"(*" not in header[3:-4] and b"*)" not in header[3:-4],
                f"{name}: embedded comment delimiter")
        expected = first + b"\n" + header + remainder
        require(sha(expected) == change["after_sha256"], f"{name}: new hash differs")
        require((SPEC / name).read_bytes() == expected,
                f"{name}: change outside the recorded header")

    for name, digest in inputs.items():
        old = original(name)
        require(sha(old) == digest, f"{name}: baseline differs from evidence")
        if name not in files:
            require((SPEC / name).read_bytes() == old, f"{name}: unchecked source edit")

    for name, digest in index["catalog_sha256"].items():
        require(sha(original(name)) == digest == sha((SPEC / name).read_bytes()),
                f"{name}: case catalog changed")

    # Also cover unused model/config files, runners and the exact tool pin.
    tracked = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", base, "orbital/spec"], cwd=ROOT,
        text=True).splitlines()
    selected = {p.removeprefix("orbital/spec/") for p in tracked
                if p.endswith((".tla", ".cfg"))}
    current = {p.relative_to(SPEC).as_posix() for p in SPEC.rglob("*")
               if p.suffix in {".tla", ".cfg"} and p.is_file()}
    require(current == selected, "model/config inventory changed")
    for name in (selected | {"check.py", "recover.py"}) - files.keys():
        require((SPEC / name).read_bytes() == original(name), f"{name}: unexpected edit")
    pin_path = "workbench/tools/tlc/source.json"
    pin = subprocess.check_output(["git", "show", f"{base}:{pin_path}"], cwd=ROOT)
    require((ROOT / pin_path).read_bytes() == pin, "TLC pin changed")
    print(f"Verified {len(files)} header insertions and {len(inputs)} checked input identities; "
          "all other model/config bytes and runners unchanged.")


if __name__ == "__main__":
    verify()
