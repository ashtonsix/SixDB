#!/usr/bin/env python3
"""Worker-side lifecycle. The script owns its experiment; this owns collection."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.request


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def extract(bundle, destination):
    with tarfile.open(bundle) as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            if not member.isfile() or path.is_absolute() or '..' in path.parts:
                raise ValueError(f'unsafe source member: {member.name}')
            output = destination.joinpath(*path.parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f'archive member has no file contents: {member.name}')
            with source, output.open('xb') as target:
                shutil.copyfileobj(source, target)
            output.chmod(member.mode & 0o777)


def metadata(path):
    base = 'http://169.254.169.254/latest/'
    request = urllib.request.Request(base + 'api/token', method='PUT',
        headers={'X-aws-ec2-metadata-token-ttl-seconds': '60'})
    with urllib.request.urlopen(request, timeout=2) as response:
        token = response.read().decode()
    request = urllib.request.Request(base + path, headers={'X-aws-ec2-metadata-token': token})
    with urllib.request.urlopen(request, timeout=2) as response:
        return json.loads(response.read())


def terminate_group(process):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    # A finished group leader can leave descendants that ignored TERM.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


class Worker:
    def __init__(self, job, base):
        self.job, self.config, self.base = job, job['config'], base
        self.results = base / 'results'
        self.results.mkdir(parents=True, exist_ok=True)
        self.source = base / 'source'
        self.source.mkdir(exist_ok=True)
        self.interrupted = threading.Event()
        self.stop = threading.Event()
        self.collection_seconds = min(120, self.config['deadline_seconds'] // 4)
        self.execution_end = job['created_at'] + self.config['deadline_seconds'] - self.collection_seconds
        self.state = {'state': 'starting', 'job': job['id']}
        self.process = None
        self.capacity = self.config.get('actual_capacity', self.config['capacity'])

    def aws(self, *args, timeout=45):
        return subprocess.run(['aws', *args, '--region', self.config['region'], '--no-cli-pager'],
                              check=True, stdout=subprocess.DEVNULL, timeout=timeout)

    def upload(self, path, suffix):
        self.aws('s3', 'cp', '--only-show-errors', str(path), self.job['uri'] + '/' + suffix)

    def status(self, state, **fields):
        self.state = self.state | fields | {'state': state, 'updated_at': time.time()}
        path = self.base / 'status.json'
        path.write_text(json.dumps(self.state, indent=2) + '\n')
        print(json.dumps(self.state), flush=True)
        self.upload(path, 'status.json')

    def monitor(self):
        # On-Demand with no live sync has no background observer during measurement.
        last_sync = time.monotonic()
        while not self.stop.wait(5):
            if self.capacity != 'on-demand':
                try:
                    notice = metadata('meta-data/spot/instance-action')
                    (self.results / 'interruption.json').write_text(json.dumps(notice) + '\n')
                    self.interrupted.set()
                    return
                except Exception:
                    pass  # A 404 means there is no notice; network errors aren't success either.
            interval = self.config['sync_seconds']
            if interval and time.monotonic() - last_sync >= interval:
                try:
                    self.sync()
                except Exception as error:
                    print(f'Live sync: {error}', flush=True)
                last_sync = time.monotonic()

    def sync(self):
        self.aws('s3', 'sync', '--only-show-errors', '--no-follow-symlinks',
                 str(self.results), self.job['uri'] + '/live/', timeout=45)

    def execute(self, argv, logfile, env=None):
        remaining = self.execution_end - time.time()
        if remaining <= 0:
            raise TimeoutError('worker execution deadline elapsed during setup')
        with logfile.open('wb') as output:
            self.process = subprocess.Popen(argv, cwd=self.source, env=env,
                stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                while True:
                    try:
                        code = self.process.wait(timeout=1)
                        return code
                    except subprocess.TimeoutExpired:
                        if self.interrupted.is_set():
                            raise InterruptedError('Spot interruption notice received')
                        if time.time() >= self.execution_end:
                            raise TimeoutError('worker execution deadline elapsed')
            finally:
                # Terminate descendants too: a script ending is not permission to keep
                # a background load alive while collection or another command proceeds.
                terminate_group(self.process)
                self.process = None

    def run(self):
        (self.results / 'job.json').write_text(json.dumps(self.job, indent=2) + '\n')
        started = time.time()
        result = {'state': 'failed', 'script_returncode': None}
        monitor = None
        try:
            self.status('downloading')
            bundle = self.results / 'source.tar.gz'
            self.aws('s3', 'cp', '--only-show-errors', self.job['uri'] + '/source.tar.gz', str(bundle))
            if digest(bundle) != self.job['source']['sha256']:
                raise ValueError('source archive checksum mismatch')
            extract(bundle, self.source)
            self.aws('s3', 'cp', '--only-show-errors', self.job['uri'] + '/source-manifest.json',
                     str(self.results / 'source-manifest.json'))
            # Existing Run helpers need a Git working tree. This synthetic commit
            # contains exactly the captured files; original HEAD remains in job.json.
            for command in (['git', 'init', '-q'], ['git', 'add', '--force', '--all'],
                ['git', '-c', 'user.name=SixDB snapshot', '-c', 'user.email=snapshot@sixdb.invalid',
                 'commit', '-qm', 'Captured worker source ' + self.job['source']['digest']]):
                subprocess.run(command, cwd=self.source, check=True, stdout=subprocess.DEVNULL)
            monitor = threading.Thread(target=self.monitor, daemon=True)
            if self.capacity != 'on-demand' or self.config['sync_seconds']:
                monitor.start()
            else:
                monitor = None
            if self.config['setup'] == 'toolchain':
                self.status('setting-up')
                code = self.execute(['bash', 'workbench/tools/worker-setup.sh'], self.results / 'setup.log')
                if code:
                    raise RuntimeError(f'toolchain setup exited {code}; see setup.log')
            host = {}
            try:
                host['instance'] = metadata('dynamic/instance-identity/document')
            except Exception as error:
                host['metadata_error'] = str(error)
            for name, command in {'cpu': ['lscpu', '--json'], 'kernel': ['uname', '-a'],
                'compiler': ['clang++-21', '--version'], 'packages': ['dpkg-query', '-W'],
                'aws': ['aws', '--version']}.items():
                try:
                    host[name] = subprocess.run(command, text=True, capture_output=True, timeout=15).stdout
                except Exception as error:
                    host[name] = str(error)
            allowed = sorted(os.sched_getaffinity(0))
            host['allowed_cpus'] = allowed
            (self.results / 'host.json').write_text(json.dumps(host, indent=2) + '\n')
            env = os.environ | {'SIXDB_CPU': str(allowed[0]), 'SIXDB_BUILD_JOBS': '1',
                'SIXDB_DATA_CACHE': str(self.base / 'datasets'), 'PYTHONDONTWRITEBYTECODE': '1'} | self.config['env']
            if int(env['SIXDB_CPU']) not in allowed:
                raise ValueError('SIXDB_CPU is outside this worker affinity mask')
            env |= {'SIXDB_RESULTS': str(self.results), 'SIXDB_SOURCE': str(self.source),
                    'SIXDB_JOB': self.job['id'], 'SIXDB_RESULTS_S3': self.job['uri'] + '/live/',
                    'SIXDB_SOURCE_COMMIT': self.job['source_commit']}
            self.status('running')
            script_started = time.time()
            command = ['bash', self.job['script'], *self.job['args']]
            if Path('/usr/bin/time').exists():
                command = ['/usr/bin/time', '-q', '-o', str(self.results / 'script.resources'),
                    '-f', 'seconds=%e\nmax_process_rss_kib=%M', *command]
            code = self.execute(command, self.results / 'script.log', env)
            result = {'state': 'complete' if code == 0 else 'failed', 'script_returncode': code,
                      'script_seconds': time.time() - script_started}
            if self.interrupted.is_set():
                result['state'] = 'interrupted'
        except Exception as error:
            result |= {'state': 'interrupted' if isinstance(error, InterruptedError) else
                       'timeout' if isinstance(error, TimeoutError) else 'failed', 'error': str(error)}
        finally:
            self.stop.set()
            if monitor:
                monitor.join(timeout=50)
        result['worker_seconds_before_collection'] = time.time() - started
        (self.results / 'worker-result.json').write_text(json.dumps(result, indent=2) + '\n')
        if (self.base / 'bootstrap.log').exists():
            shutil.copyfile(self.base / 'bootstrap.log', self.results / 'bootstrap.log')
        # Freeze outputs before packing; only phase markers remain mutable outside results.
        try:
            self.status('uploading', **{k: v for k, v in result.items() if k != 'state'})
            sys.path.insert(0, str(self.source / 'workbench/tools'))
            import artifacts
            issues = []
            for receipt in sorted(self.results.rglob('run.json')):
                try:
                    data = json.loads(receipt.read_text())
                except (ValueError, UnicodeError):
                    continue
                try:
                    if not isinstance(data, dict) or 'source_files_sha256' not in data:
                        continue  # An arbitrary script can also have a file called run.json.
                    if data.get('inputs'):
                        refs = artifacts.publish_inputs(receipt.parent)
                        (receipt.parent / 'input-artifacts.json').write_text(json.dumps(refs, indent=2) + '\n')
                except Exception as error:
                    issues.append({'run': str(receipt.relative_to(self.results)), 'error': str(error)})
            if issues:
                (self.results / 'input-collection-errors.json').write_text(json.dumps(issues, indent=2) + '\n')
                if result['state'] == 'complete':
                    result['state'] = 'failed'
                result['error'] = 'Some prepared inputs could not be retained; raw output preserved'
                (self.results / 'worker-result.json').write_text(json.dumps(result, indent=2) + '\n')
            # Keep diagnostic logs directly accessible. Normal result files are
            # stored once in the final bundle, not also mirrored under live/.
            for path in sorted(self.results.iterdir()):
                if path.is_file() and (path.suffix in {'.log', '.resources'} or path.name == 'worker-result.json'):
                    self.upload(path, 'live/' + path.name)
            with tempfile.TemporaryDirectory(dir=self.base) as temp:
                reference = artifacts.publish(self.results, Path(temp), bucket=self.config['bucket'],
                                              region=self.config['region'], validate_run=False)
            self.status(result['state'], artifact=reference, **{k: v for k, v in result.items() if k != 'state'})
        except Exception as error:
            try:
                self.sync()  # Preserve recoverable raw files if bundling fails.
            except Exception as partial_error:
                print(f'Partial collection failed: {partial_error}', flush=True)
            try:
                self.status('upload-failed', error=str(error), script_result=result)
            except Exception:
                print(f'Collection failed: {error}', flush=True)
            return 1
        return 0 if result['state'] == 'complete' else 1


if __name__ == '__main__':
    job_path = Path(sys.argv[1])
    sys.exit(Worker(json.loads(job_path.read_text()), job_path.parent).run())
