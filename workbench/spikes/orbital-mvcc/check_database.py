"""Give each comparator check a fresh database, including repeated CTest runs."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="orbital-mvcc-check-") as directory:
    result = subprocess.run([sys.argv[1], str(Path(directory) / "db")], check=False)
raise SystemExit(result.returncode)
