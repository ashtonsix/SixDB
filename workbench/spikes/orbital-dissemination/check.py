"""Run the independent numerical, causal and failure-history checks."""
from pathlib import Path
import subprocess
import sys


if __name__ == "__main__":
    for script in sorted(Path(__file__).parent.glob("check_*.py")):
        print(script.name, flush=True)
        subprocess.run([sys.executable, str(script)], check=True)
