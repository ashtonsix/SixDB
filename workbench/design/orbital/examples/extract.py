"""Compile a literate example's cpp fences in document order."""

import json
from pathlib import Path
import re
import sys


def extract(source: Path) -> str:
    chunks = []
    fence = None
    cpp = False
    for number, line in enumerate(source.read_text().splitlines(keepends=True), 1):
        marker = re.fullmatch(r"(`{3,}|~{3,})([^\n]*)\n?", line)
        if fence is None:
            if marker:
                fence = marker[1]
                cpp = marker[2].strip() == "cpp"
                if cpp:
                    chunks.append(f"#line {number + 1} {json.dumps(str(source.resolve()))}\n")
        elif marker and marker[1][0] == fence[0] and len(marker[1]) >= len(fence) and not marker[2].strip():
            fence = None
            cpp = False
        elif cpp:
            chunks.append(line if line.endswith("\n") else line + "\n")
    if fence is not None:
        raise ValueError(f"{source}: unclosed code fence")
    if not chunks:
        raise ValueError(f"{source}: no cpp blocks")
    return "".join(chunks)


if __name__ == "__main__":
    source, destination = map(Path, sys.argv[1:])
    content = extract(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text() != content:
        destination.write_text(content)
