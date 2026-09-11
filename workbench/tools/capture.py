#!/usr/bin/env python3
"""Capture current sources in a new worktree, optionally replacing a few inputs."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess

from experiment import sha256, source_files

ROOT = Path(__file__).resolve().parents[2]


def checkout_path(value):
    path = Path(value).resolve()
    root = Path(path.anchor)
    existing = path
    while not existing.exists():
        existing = existing.parent
    # OrbStack also mounts the Linux root under its Mac-facing path. resolve()
    # cannot collapse bind mounts; Git records their spellings literally and
    # later fails to find/remove a worktree addressed through the other path.
    for ancestor in (path, *path.parents):
        if ancestor.exists() and os.path.samefile(ancestor, root):
            counterpart = root / existing.relative_to(ancestor)
            # Submounts (often /tmp) need not be shared by the root alias.
            if counterpart.exists() and os.path.samefile(existing, counterpart):
                return root / path.relative_to(ancestor)
            return path
    return path


def python_cache(value):
    path = PurePosixPath(value)
    return '__pycache__' in path.parts or path.suffix in ('.pyc', '.pyo')


def source_name(value):
    path = PurePosixPath(value)
    if not path.parts or path.is_absolute() or any(p in ('.git', '..') for p in path.parts):
        raise ValueError(f'expected a repository-relative source path: {value}')
    name = path.as_posix()
    if python_cache(name) or name.startswith('build/') or name == 'build' or (
            name.startswith('workbench/spikes/') and 'evidence' in path.parts):
        raise ValueError(f'{value} is an output path; map the input to a source path in the capture')
    return name


def receipt_path(destination):
    return destination.with_name(destination.name + '.capture.json')


def identity(files):
    hashes = {name: sha256(data) for name, data in sorted(files.items())}
    return hashes, sha256(json.dumps(hashes, sort_keys=True).encode())


def capture(base, destination, *, replacements=(), removed=(), expected=None, inputs=None):
    base, destination = checkout_path(base), checkout_path(destination)
    inputs = checkout_path(inputs or base)
    receipt = receipt_path(destination)
    top = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], cwd=base, text=True).strip()
    if not os.path.samefile(base, top):
        raise ValueError('base must be the root of a Git checkout')
    if '.git' in destination.parts:
        raise ValueError('capture destination must be outside Git metadata')
    if destination.exists() or receipt.exists():
        raise ValueError('choose a new capture destination; the existing capture is untouched')
    files = source_files(base)
    _, base_digest = identity(files)
    files = {name: data for name, data in files.items() if not python_cache(name)}
    modes = {name: 0o755 if (base / name).stat().st_mode & 0o111 else 0o644 for name in files}
    changes = []

    def discard(name):
        names = [p for p in files if p == name or p.startswith(name + '/')]
        for p in names:
            del files[p]
            del modes[p]
        return names

    for value in removed:
        name = source_name(value)
        if not discard(name):
            raise ValueError(f'cannot drop missing source: {name}')
        changes.append({'drop': name})
    for value in replacements:
        target, separator, origin = value.partition('=')
        name = source_name(target)
        source = Path(origin if separator else target)
        source = (inputs / source).resolve()
        if not source.exists():
            raise ValueError(f'missing replacement input: {source}')
        discard(name)
        paths = sorted(source.rglob('*')) if source.is_dir() else [source]
        for path in paths:
            if source.is_dir() and python_cache(path.relative_to(source).as_posix()):
                continue
            if path.is_symlink():
                raise ValueError(f'replacement symlinks are unsupported: {path}')
            if path.is_dir():
                continue
            if not path.is_file():
                raise ValueError(f'replacement is not a regular file: {path}')
            relative = source_name(str(PurePosixPath(name) / path.relative_to(source))) if source.is_dir() else name
            files[relative] = path.read_bytes()
            modes[relative] = 0o755 if path.stat().st_mode & 0o111 else 0o644
        changes.append({'replace': name, 'from': str(source), 'directory': source.is_dir()})
    hashes, digest = identity(files)
    for name in files:
        if any(parent.as_posix() in files for parent in PurePosixPath(name).parents):
            raise ValueError(f'a replacement file is also the parent of {name}')
    for name, wanted in (expected or {}).items():
        name = source_name(name)
        if hashes.get(name) != wanted:
            raise ValueError(f'source hash mismatch: {name}; expected {wanted}, captured {hashes.get(name, "missing")}')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=base, text=True).strip()
    common = subprocess.check_output(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                     cwd=base, text=True).strip()
    worktree_git = ['git', '--git-dir', str(checkout_path(common)), 'worktree']
    destination.parent.mkdir(parents=True, exist_ok=True)
    created = False
    receipt_created = False
    try:
        subprocess.run([*worktree_git, 'add', '--quiet', '--detach', str(destination), commit], cwd=base, check=True)
        created = True
        tracked = subprocess.check_output(['git', 'ls-files', '--cached', '-z'], cwd=destination).decode().split('\0')
        for name in filter(None, tracked):
            path = destination / name
            if path.is_symlink() or (name not in files and path.is_file()):
                path.unlink()
        for name, data in files.items():
            path = destination / name
            if path.is_dir():
                shutil.rmtree(path)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            path.chmod(modes[name])
        # Explicit replacements can originate in ignored prototype directories.
        # Record their destination names without committing or touching the live index.
        subprocess.run(['git', 'add', '--intent-to-add', '--force', '--pathspec-from-file=-', '--pathspec-file-nul'],
                       input=b''.join(name.encode() + b'\0' for name in files), cwd=destination, check=True)
        if source_files(destination) != files:
            raise ValueError('captured source selection differs from the requested inputs')
        metadata = {'format': 1, 'source_commit': commit, 'base': str(base), 'base_digest': base_digest,
                    'changes': changes, 'source_digest': digest, 'source_files_sha256': hashes,
                    'source_modes': modes}
        with receipt.open('x') as out:
            receipt_created = True
            out.write(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
        return metadata
    except BaseException:
        if receipt_created:
            receipt.unlink()
        if created:
            subprocess.run([*worktree_git, 'remove', '--force', str(destination)], cwd=base, check=True)
        raise


def verify(destination):
    destination = checkout_path(destination)
    metadata = json.loads(receipt_path(destination).read_text())
    hashes, digest = identity(source_files(destination))
    changed = sorted(name for name in hashes.keys() | metadata['source_files_sha256'].keys()
                     if hashes.get(name) != metadata['source_files_sha256'].get(name))
    changed += [name for name in hashes if name not in changed and
                (0o755 if (destination / name).stat().st_mode & 0o111 else 0o644) != metadata['source_modes'][name]]
    if changed or digest != metadata['source_digest']:
        raise ValueError('capture changed: ' + ', '.join(changed[:12]))
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('create', help='freeze the current checkout or derive from an earlier capture')
    create.add_argument('output', type=Path)
    create.add_argument('--base', type=Path, default=ROOT, help='source checkout; defaults to this repository')
    create.add_argument('--replace', action='append', default=[], metavar='PATH[=INPUT]',
                        help='replace a file or whole directory; INPUT defaults to PATH in the live repository')
    create.add_argument('--drop', action='append', default=[], metavar='PATH', help='omit a source file or directory')
    create.add_argument('--expect', action='append', default=[], metavar='PATH=SHA256',
                        help='optionally check an owner-provided hash before creating anything')
    check = commands.add_parser('verify', help='compare current capture bytes and executable bits with its receipt')
    check.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'verify':
            metadata = verify(args.output)
        else:
            expected = {}
            for value in args.expect:
                name, separator, digest = value.partition('=')
                if not separator or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
                    raise ValueError('--expect takes PATH=SHA256')
                expected[source_name(name)] = digest
            metadata = capture(args.base, args.output, replacements=args.replace, removed=args.drop,
                               expected=expected, inputs=ROOT)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')
    output = checkout_path(args.output)
    print(f"{output}: {len(metadata['source_files_sha256'])} files; {metadata['source_digest']}")
    print(f'Receipt: {receipt_path(output)}')


if __name__ == '__main__':
    main()
