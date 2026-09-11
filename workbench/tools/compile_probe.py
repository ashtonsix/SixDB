#!/usr/bin/env python3
"""Measure selected Clang object compilations serially without replacing build outputs."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def absolute(path, directory):
    return (directory / path).resolve()


def select(database, sources, objects):
    entries = []
    for row in json.loads(database.read_text()):
        directory = absolute(row['directory'], database.parent)
        argv = row.get('arguments') or shlex.split(row['command'])
        output = row.get('output')
        if not output and '-o' in argv:
            output = argv[argv.index('-o') + 1]
        entries.append({'source': str(absolute(row['file'], directory)),
                        'object': str(absolute(output, directory)) if output else '',
                        'directory': str(directory), 'argv': argv})
    chosen = []
    for field, selectors in [('source', sources), ('object', objects)]:
        for selector in selectors:
            spelling = str(Path(selector))
            matches = [e for e in entries if e[field] == spelling or
                       (not Path(spelling).is_absolute() and e[field].endswith('/' + spelling))]
            if len(matches) != 1:
                details = '\n'.join(e['object'] for e in matches)
                raise ValueError(f'{field} {selector!r} matches {len(matches)} commands. '
                                 f'Use a longer suffix or --object.\n{details}')
            if matches[0] in chosen:
                raise ValueError(f'Duplicate command selected: {selector}')
            chosen.append(matches[0])
    return chosen


def probe_command(entry, output, trace=False):
    """Redirect known compiler outputs; keep compile flags and working directory."""
    argv = entry['argv']
    if not argv or 'clang' not in Path(argv[0]).name or argv.count('-c') != 1 or argv.count('-o') != 1:
        raise ValueError('Expected a direct Clang -c command with one -o; compiler launchers are not replayed')
    # These modes can hide output paths or write beside sources/in shared caches.
    unsupported = ('@', '-save-temps', '-fmodules', '-fmodule-', '-emit-pch',
                   '-fprofile-instr-generate', '-fprofile-generate', '-ftest-coverage',
                   '--coverage', '-foptimization-record-file', '-save-stats')
    if any(a.startswith(unsupported) or a in {'&&', ';', '|', '>', '<'} for a in argv):
        raise ValueError('Probe ordinary object commands without response files, shell operators, '
                         'modules, profiling or extra side-output modes')
    replacements = {'-o': output / 'object.o', '-MF': output / 'dependencies.d',
                    '-MT': output / 'object.o', '-MQ': output / 'object.o',
                    '-MJ': output / 'compilation.json',
                    '--serialize-diagnostics': output / 'diagnostics.dia'}
    result, i = [], 0
    while i < len(argv):
        arg = argv[i]
        if arg in replacements:
            if i + 1 == len(argv):
                raise ValueError(f'Missing value after {arg}')
            result.extend([arg, str(replacements[arg])]); i += 2
            continue
        joined = next((key for key in ('-MF', '-MT', '-MQ', '-MJ')
                       if arg.startswith(key) and arg != key), None)
        if joined:
            result.append(joined + str(replacements[joined]))
        elif arg == '-ftime-trace' or arg.startswith('-ftime-trace='):
            result.append('-ftime-trace=' + str(output / 'trace.json'))
        elif arg.startswith('--serialize-diagnostics='):
            result.append('--serialize-diagnostics=' + str(output / 'diagnostics.dia'))
        else:
            result.append(arg)
        i += 1
    if trace and not any(a.startswith('-ftime-trace=') for a in result):
        result.append('-ftime-trace=' + str(output / 'trace.json'))
    return result


def cgroup_snapshot():
    """Read the inherited cgroup; never create groups or reset counters."""
    try:
        member = next(line[3:] for line in Path('/proc/self/cgroup').read_text().splitlines()
                      if line.startswith('0::'))
        for line in Path('/proc/self/mountinfo').read_text().splitlines():
            left, right = line.split(' - ', 1)
            if right.split()[0] != 'cgroup2':
                continue
            fields = left.split()
            unescape = lambda s: re.sub(r'\\([0-7]{3})', lambda m: chr(int(m[1], 8)), s)
            root, mount = Path(unescape(fields[3])), Path(unescape(fields[4]))
            path = mount / Path(member).relative_to(root)
            counters = dict(line.split() for line in (path / 'memory.events').read_text().splitlines())
            return {'path': str(path), 'events': {k: int(v) for k, v in counters.items()}}
        return {'unavailable': 'No cgroup v2 memory controller found'}
    except (OSError, ValueError, StopIteration) as error:
        return {'unavailable': str(error) or 'No cgroup v2 membership'}


def kernel_snapshot():
    program = shutil.which('dmesg')
    if not program:
        return {'unavailable': 'dmesg not installed'}
    try:
        result = subprocess.run([program, '--color=never', '--raw'], capture_output=True,
                                text=True, errors='replace', timeout=5)
        if result.returncode:
            return {'unavailable': result.stderr.strip()[:500]}
        return {'records': [line for line in result.stdout.splitlines()
                            if re.search(r'out of memory|oom-kill|killed process', line, re.I)]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'unavailable': str(error)}


def oom_evidence(before, after, kernel_before, kernel_after, pid):
    result = {'scope': 'Inherited cgroup and host-wide kernel log; unrelated tasks may contribute.',
              'compiler_pid_confirmed': False}
    if 'events' in before and before.get('path') == after.get('path') and 'events' in after:
        result['cgroup'] = {'before': before, 'after': after,
                            'delta': {k: v - before['events'].get(k, v)
                                      for k, v in after['events'].items()}}
    else:
        result['cgroup'] = {'before': before, 'after': after}
    if 'records' in kernel_before and 'records' in kernel_after:
        previous = Counter(kernel_before['records'])
        new = []
        for line in kernel_after['records']:
            if previous[line]:
                previous[line] -= 1
            else:
                new.append(line)
        result['new_kernel_records'] = new
        result['compiler_pid_confirmed'] = any(
            re.search(r'\bKilled process ' + str(pid) + r'\b', line, re.I) for line in new)
    else:
        result['kernel_log'] = {'before': kernel_before.get('unavailable'),
                                'after': kernel_after.get('unavailable')}
    return result


def compile_one(entry, output, argv):
    output.mkdir()
    source = Path(entry['source'])
    record = {'source': str(source), 'source_sha256_before': digest(source),
              'directory': entry['directory'], 'original_object': entry['object'],
              'original_argv': entry['argv'], 'argv': argv, 'output': output.name}
    before, kernel_before = cgroup_snapshot(), kernel_snapshot()
    interrupted = False
    with (output / 'stdout.txt').open('wb') as stdout, (output / 'stderr.txt').open('wb') as stderr:
        started = time.monotonic()
        process = subprocess.Popen(argv, cwd=entry['directory'], stdout=stdout,
                                   stderr=stderr, start_new_session=True)
        try:
            _, status, usage = os.wait4(process.pid, 0)
        except KeyboardInterrupt:
            interrupted = True
            # Stop only this probe's process group; retain its resource/failure record.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            _, status, usage = os.wait4(process.pid, 0)
        process.returncode = os.waitstatus_to_exitcode(status)
        record.update(pid=process.pid, returncode=process.returncode,
                      wall_seconds=time.monotonic() - started,
                      user_seconds=usage.ru_utime, system_seconds=usage.ru_stime,
                      max_process_rss_kib=usage.ru_maxrss, interrupted=interrupted)
    record['oom_evidence'] = oom_evidence(before, cgroup_snapshot(), kernel_before,
                                           kernel_snapshot(), process.pid)
    try:
        record['source_sha256_after'] = digest(source)
    except OSError as error:
        record['source_sha256_after'] = None
        record['source_read_error'] = str(error)
    record['source_unchanged'] = record['source_sha256_before'] == record['source_sha256_after']
    if process.returncode == 0 and (output / 'object.o').is_file():
        record['object'] = {'bytes': (output / 'object.o').stat().st_size,
                            'sha256': digest(output / 'object.o')}
    record['status'] = ('complete' if process.returncode == 0 and record['source_unchanged']
                        and 'object' in record else 'failed')
    save(output / 'compile.json', record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='build directory or compile_commands.json')
    parser.add_argument('--source', action='append', default=[], help='exact source path or unique path suffix; repeat for serial compiles')
    parser.add_argument('--object', action='append', default=[], help='select by original object path/suffix when a source has several commands')
    parser.add_argument('--output', type=Path, required=True, help='new directory for receipts, logs and isolated objects')
    parser.add_argument('--time-trace', action='store_true', help='add Clang time tracing; records this change to the command')
    parser.add_argument('--plan', action='store_true', help='print selected commands without creating files or compiling')
    args = parser.parse_args()
    if not args.source and not args.object:
        parser.error('Select at least one --source or --object')
    database = args.build.resolve()
    if database.is_dir():
        database /= 'compile_commands.json'
    output = args.output.resolve()
    try:
        entries = select(database, args.source, args.object)
        commands = [probe_command(e, output / f'{i:02d}-{Path(e["source"]).stem}', args.time_trace)
                    for i, e in enumerate(entries)]
        if output.exists():
            raise ValueError(f'Output already exists: {output}; choose a new directory')
        for entry in entries:
            if not Path(entry['source']).is_file() or not Path(entry['directory']).is_dir():
                raise ValueError('Compile database source/working directory is unavailable; configure the recovered source checkout first')
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.error(str(error))
    if args.plan:
        print(json.dumps([dict(e, probe_argv=a) for e, a in zip(entries, commands)], indent=2))
        return 0
    if sys.platform != 'linux':
        parser.error('Compile measurement uses Linux wait4 RSS units; run on the pinned Linux toolchain')
    output.mkdir(parents=True)
    started = time.monotonic()
    report = {'format': 1, 'status': 'running', 'started_utc': datetime.now(timezone.utc).isoformat(),
              'database': str(database), 'database_sha256': digest(database),
              'selected_commands': entries, 'host': list(os.uname()),
              'affinity_cpus': sorted(os.sched_getaffinity(0)),
              'source_capture': {k: os.environ[k] for k in ('SIXDB_JOB', 'SIXDB_SOURCE_COMMIT') if k in os.environ},
              'limits': 'Forced serial compiler invocations, without linking. OS caches are not cleared. '
                        'RSS is a process maximum, not concurrent aggregate memory. TU hashes do not cover '
                        'headers: use a captured checkout for source identity. Redirected outputs can affect '
                        'debug paths/object bytes; these are diagnostic objects, not the measured runtime binaries. '
                        'Missing OOM evidence does not exclude OOM. A driver child killed by OOM may not match '
                        'the recorded compiler PID. This helper does not execute Ninja or modify its log.',
              'compiles': []}
    save(output / 'summary.json', report)
    try:
        report['compilers'] = {}
        for entry in entries:
            executable = entry['argv'][0]
            if '/' in executable:
                executable = str(absolute(executable, Path(entry['directory'])))
            else:
                executable = shutil.which(executable) or executable
            if executable not in report['compilers']:
                version = subprocess.run([executable, '--version'], stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, timeout=10)
                report['compilers'][executable] = {'version': version.stdout[:4096],
                                                   'returncode': version.returncode}
        for i, (entry, argv) in enumerate(zip(entries, commands)):
            record = compile_one(entry, output / f'{i:02d}-{Path(entry["source"]).stem}', argv)
            report['compiles'].append(record)
            save(output / 'summary.json', report)
            print(f'{Path(entry["source"]).name}: {record["wall_seconds"]:.2f}s, '
                  f'{record["max_process_rss_kib"] / 1024:.1f} MiB process max, {record["status"]}', flush=True)
            if record['status'] != 'complete':
                break
        report['status'] = ('complete' if len(report['compiles']) == len(entries)
                            and all(r['status'] == 'complete' for r in report['compiles']) else 'failed')
    except (OSError, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        report.update(status='failed', error=str(error) or 'Interrupted')
    finally:
        report['elapsed_seconds'] = time.monotonic() - started
        for field in ('wall_seconds', 'user_seconds', 'system_seconds'):
            report['serial_compile_' + field] = sum(r[field] for r in report['compiles'])
        report['max_process_rss_kib'] = max((r['max_process_rss_kib'] for r in report['compiles']), default=None)
        save(output / 'summary.json', report)
    print(f'{report["status"]}: {output / "summary.json"}')
    return 0 if report['status'] == 'complete' else 1


if __name__ == '__main__':
    sys.exit(main())
