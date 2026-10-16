#!/usr/bin/env python3
"""Account for all local build output; expire only idle CMake objects under pressure."""
import argparse
import fcntl
import hashlib
import glob
import os
from pathlib import Path
import stat
import subprocess
import time

import storage
import worker_cache
from dev import checkout_root


def state_path(root):
    root = checkout_root(root, Path.home())
    key = hashlib.sha256(str(root).encode()).hexdigest()[:12]
    return Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / f'sixdb/local-build-cache-{key}.json'


def build_roots(root):
    roots = []
    # Canonicalize *before* ordering: an OrbStack alias can be longer than a
    # nested worktree. The enclosing build tree must win over all nested roots.
    checkouts = {checkout_root(p, Path.home()) for p in worker_cache.checkouts(root) if p.is_dir()}
    for checkout in sorted(checkouts, key=lambda p: (len(p.parts), str(p))):
        build = checkout / 'build'
        if build.is_dir() and not build.is_symlink() and not any(build.is_relative_to(p) for p in roots):
            if not any(build.samefile(p) for p in roots):
                roots.append(build)
    return roots


def compiler_running():
    """A compiler/build controller anywhere in this guest defers automatic unlinking."""
    if not Path('/proc').is_dir():
        return True
    for path in Path('/proc').glob('[0-9]*/comm'):
        try:
            name = path.read_text().strip()
        except FileNotFoundError:
            continue
        except OSError:
            return True
        if (name in {'ninja', 'make', 'gmake', 'cmake', 'cc', 'c++', 'gcc', 'g++',
                     'cc1', 'cc1plus', 'ld', 'ld.lld', 'lld', 'lto-wrapper'} or
                name.startswith(('clang-', 'clang++', 'gcc-', 'g++-'))):
            return True
    return False


def inventory(root, *, now=None):
    now = time.time() if now is None else now
    days = float(os.environ.get('SIXDB_LOCAL_BUILD_DAYS', '7'))
    if days < 0:
        raise ValueError('SIXDB_LOCAL_BUILD_DAYS must be nonnegative')
    cutoff = now - days * 86400
    groups, cmakes, candidates, errors = {}, {}, {}, []
    workers = {checkout_root(p, Path.home()) for p in worker_cache.roots(root)}
    builds = build_roots(root)
    if not builds:
        return {'bytes': 0, 'groups': {}, 'candidates': [], 'idle_days': days, 'errors': []}
    # One native scan counts directories, sparse allocation and hardlinks across
    # all operands. Keeping millions of Python inode/path objects is unnecessary.
    command = ['du', '-x', '-B1', '--max-depth=1', '--null']
    excludes = {glob.escape(str(p)) for p in workers}
    for build in builds:
        conventional = {p for p in workers if p.is_relative_to(build) and
                        len(p.relative_to(build).parts) == 4 and
                        p.relative_to(build).parts[0] == 'workspaces' and
                        p.relative_to(build).parts[-2:] == ('build', 'workers')}
        if conventional:
            # These exact conventional roots are all covered by worker_cache.roots.
            # One pattern avoids matching dozens of long prefixes for every file.
            excludes -= {glob.escape(str(p)) for p in conventional}
            excludes.add(glob.escape(str(build)) + '/workspaces/*/build/workers')
    command += ['--exclude=' + p for p in sorted(excludes)]
    result = subprocess.run(command + ['--', *map(str, builds)], capture_output=True)
    if result.returncode:
        errors.append(os.fsdecode(result.stderr).strip())
    totals = {}
    for row in result.stdout.split(b'\0'):
        size, separator, name = row.partition(b'\t')
        if separator and size.isdigit():
            path = Path(os.fsdecode(name))
            if path in builds:
                totals[path] = int(size)
            elif path.parent in builds:
                groups[str(path)] = int(size)
    for build in builds:
        if build not in totals:
            errors.append(f'No disk-usage total for {build}')
        # Include files directly under build/, rather than hiding them between groups.
        children = sum(size for name, size in groups.items() if Path(name).parent == build)
        groups[str(build)] = max(0, totals.get(build, 0) - children)
        tracked_result = subprocess.run(['git', '-C', str(build.parent), 'ls-files', '-z', '--', 'build'],
                                        capture_output=True, text=True)
        tracked = {build.parent / p for p in tracked_result.stdout.split('\0') if p} if tracked_result.returncode == 0 else set()
        def scan_error(error):
            errors.append(str(error))
        # Discovery needs names only. Pinned/Git trees still count above, but are
        # never traversed as possible automatic deletion candidates.
        for directory, children, names in os.walk(build, followlinks=False, onerror=scan_error):
            directory = Path(directory)
            if (directory in workers or (directory / '.keep-local').exists() or (directory / '.git').exists()):
                children[:] = []
                continue
            children[:] = [n for n in children if not (directory / n).is_symlink()]
            if 'CMakeCache.txt' in names and not (directory / 'CMakeCache.txt').is_symlink():
                cmakes[directory] = (build, tracked)
    for cmake, (build, tracked) in sorted(cmakes.items(), key=lambda p: -len(p[0].parts)):
        try:
            recent = subprocess.run(['find', str(cmake), '-xdev', '-newermt', f'@{cutoff:.6f}', '-print', '-quit'],
                                    capture_output=True)
            if recent.returncode:
                errors.append(os.fsdecode(recent.stderr).strip())
                continue
            if recent.stdout:
                continue
            config = (cmake / 'CMakeCache.txt').stat()
            objects = subprocess.run(['find', str(cmake), '-xdev', '-name', '.git', '-prune', '-o',
                '-type', 'f', '(', '-name', '*.o', '-o', '-name', '*.obj', '-o', '-name', '*.pch',
                '-o', '-name', '*.gch', '-o', '-name', '*.pcm', ')', '-path', '*/CMakeFiles/*', '-print0'],
                capture_output=True)
            if objects.returncode:
                errors.append(os.fsdecode(objects.stderr).strip())
                continue
            for name in objects.stdout.split(b'\0'):
                if not name:
                    continue
                path = Path(os.fsdecode(name))
                if path in candidates or path in tracked or pinned(path, build):
                    continue
                info = path.lstat()
                if stat.S_ISREG(info.st_mode) and info.st_nlink == 1:
                    group = str(build / path.relative_to(build).parts[0])
                    candidates[path] = {'path': str(path), 'cmake': str(cmake), 'group': group,
                        'allocated_bytes': info.st_blocks * 512,
                        'cache_identity': [config.st_dev, config.st_ino, config.st_size, config.st_mtime_ns, config.st_ctime_ns],
                        'identity': [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns]}
        except OSError as error:
            errors.append(f'{cmake}: {error}')
    return {'bytes': sum(totals.values()), 'groups': groups, 'candidates': list(candidates.values()),
            'idle_days': days, 'errors': errors}


def pinned(path, build):
    return any((p / '.keep-local').exists() or (p / '.git').exists()
               for p in [path.parent, *path.parent.parents] if p == build or p.is_relative_to(build))


def reclaim(candidate):
    path, cmake = Path(candidate['path']), Path(candidate['cmake'])
    if pinned(path, cmake) or any((p / '.keep-local').exists() for p in cmake.parents):
        return 0
    try:
        info = path.lstat()
        if [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns] != candidate['identity']:
            return 0
        cache = cmake / 'CMakeCache.txt'
        config = cache.lstat()
        if (path.is_symlink() or path.resolve() != path or info.st_nlink != 1 or cache.is_symlink() or
                [config.st_dev, config.st_ino, config.st_size, config.st_mtime_ns, config.st_ctime_ns] != candidate['cache_identity']):
            return 0
        # A suffix alone is not sufficient: notes named *.o are not compiler output.
        with path.open('rb') as stream:
            prefix = stream.read(4)
        if prefix not in {b'\x7fELF', b'BC\xc0\xde', b'CPCH'}:
            return 0
        current = path.lstat()
        if [current.st_dev, current.st_ino, current.st_size, current.st_mtime_ns,
                current.st_ctime_ns, current.st_nlink] != [*candidate['identity'], 1]:
            return 0
        path.unlink()
        storage.sync_directory(path.parent)
        return candidate['allocated_bytes']
    except FileNotFoundError:
        return 0


def maintain(root, *, prune=True):
    report_path = state_path(root)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.with_suffix('.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return None
        scan = inventory(root)
        budget = int(float(os.environ.get('SIXDB_LOCAL_BUILD_GIB', '60')) * storage.GIB)
        if budget < 0:
            raise ValueError('SIXDB_LOCAL_BUILD_GIB must be nonnegative')
        free = min(i['free'] for i in storage.headroom(root).values())
        removed = []
        busy = compiler_running() if prune else False
        if prune and not busy:
            for candidate in sorted(scan['candidates'], key=lambda c: c['identity'][3]):
                if scan['bytes'] <= budget and free >= 20 * storage.GIB:
                    break
                # Recheck between build trees, so a newly started compiler defers the rest.
                if not removed or removed[-1]['cmake'] != candidate['cmake']:
                    if compiler_running():
                        busy = True
                        break
                count = reclaim(candidate)
                if count:
                    scan['bytes'] -= count
                    scan['groups'][candidate['group']] -= count
                    free += count
                    removed.append(candidate)
        report = {k: v for k, v in scan.items() if k != 'candidates'}
        report.update(checked_at=time.time(), budget_bytes=budget, removed=removed,
            removed_bytes=sum(c['allocated_bytes'] for c in removed), compiler_running=busy,
            eligible_object_bytes=sum(c['allocated_bytes'] for c in scan['candidates']),
            over_budget=scan['bytes'] > budget, below_free_target=free < 20 * storage.GIB)
        storage.write_json(report_path, report)
        print(f'Local build output: {scan["bytes"] / storage.GIB:.2f} GiB; '
              f'budget {budget / storage.GIB:g} GiB (workers counted separately).', flush=True)
        if removed:
            print(f'Reclaimed {report["removed_bytes"] / storage.GIB:.2f} GiB of idle CMake objects; '
                  'missing objects rebuild normally.', flush=True)
        if report['over_budget'] or scan['errors']:
            print(f'Local build storage needs review: {report_path}. '
                  'Pinned/recent/unique output remains; Tooling follows unresolved growth.', flush=True)
        if not prune:
            for path, size in sorted(scan['groups'].items(), key=lambda p: -p[1])[:10]:
                print(f'{size / storage.GIB:6.2f} GiB  {path}')
        return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prune', action='store_true')
    args = parser.parse_args()
    root = checkout_root(Path(__file__).resolve().parents[2], Path.home())
    maintain(root, prune=args.prune)
