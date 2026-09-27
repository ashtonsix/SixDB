"""Named checks, with sibling family catalogs kept as the single source of cases.

Module/configuration paths are relative to orbital/spec, as in suite.py; include
paths are relative to their catalog. Loading never reads models or starts TLC.
"""
from __future__ import annotations

import json
import re
from pathlib import Path


def read_catalog(manifest: Path) -> tuple[list[dict], dict[Path, bytes]]:
    """Return cases and the exact catalog bytes from which they were selected."""
    sources: dict[Path, bytes] = {}
    active: list[Path] = []

    def visit(path: Path) -> list[dict]:
        path = path.resolve()
        if path in active:
            raise ValueError("Catalog include cycle: " + " -> ".join(p.name for p in [*active, path]))
        active.append(path)
        if path not in sources:
            sources[path] = path.read_bytes()
        document = json.loads(sources[path])
        cases = []
        for include in document.get("includes", []):
            if Path(include).is_absolute():
                raise ValueError(f"Catalog includes must be relative: {path}: {include}")
            cases.extend(visit(path.parent / include))
        cases.extend(document.get("cases", []))
        active.pop()
        return cases

    cases = visit(manifest)
    names = set()
    for case in cases:
        name = case["name"]
        if name in names:
            raise ValueError(f"Duplicate case name in catalog: {name}")
        names.add(name)
        if "heap" in case and (not isinstance(case["heap"], str) or
                               not re.fullmatch(r"[1-9][0-9]*[mMgG]", case["heap"])):
            raise ValueError(f"Invalid heap for {name}: use an explicit m or g suffix, for example 512m")
    return cases, sources


def load_cases(manifest: Path) -> list[dict]:
    return read_catalog(manifest)[0]


def catalog_paths(manifest: Path) -> list[Path]:
    return list(read_catalog(manifest)[1])
