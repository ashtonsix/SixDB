#!/usr/bin/env python3
"""Screen clangd parser errors in project C++ files, including unbuilt headers."""

import argparse
from pathlib import Path
import re
import subprocess

from dev import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*', type=Path, help='Files to check; default: all Git-visible C++ files')
    parser.add_argument('--clangd', default='clangd-21', help='Language server executable')
    args = parser.parse_args()
    if args.files:
        files = [path if path.is_absolute() else ROOT / path for path in args.files]
    else:
        result = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
                                cwd=ROOT, check=True, stdout=subprocess.PIPE, text=True)
        suffixes = {'.h', '.hh', '.hpp', '.hxx', '.cc', '.cpp', '.cxx'}
        files = sorted({ROOT / name for name in result.stdout.split('\0')
                        if Path(name).suffix in suffixes and (ROOT / name).is_file()})
    output = ROOT / 'build/ide-check'
    output.mkdir(parents=True, exist_ok=True)
    failures = []
    for index, path in enumerate(files):
        # Disable clangd's developer-only refactoring smoke tests: missing an
        # outline destination for an inline header is not a parser diagnostic.
        result = subprocess.run([args.clangd, '--check=' + str(path), '--tweaks='], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log = output / f'{index:03d}-{path.name}.log'
        log.write_text(result.stdout)
        name = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
        print(f'{"FAIL" if result.returncode else "PASS"} {name}', flush=True)
        if result.returncode:
            failures.append(name)
            for line in result.stdout.splitlines():
                if re.match(r'E\[', line):
                    print('  ' + line, flush=True)
    print(f'{len(files)} files checked; {len(failures)} failed. Logs: {output}')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
