#!/usr/bin/env python3
"""Worker-side lifecycle. The script owns its experiment; this owns collection."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
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


def metadata(path, *, text=False):
    base = 'http://169.254.169.254/latest/'
    request = urllib.request.Request(base + 'api/token', method='PUT',
        headers={'X-aws-ec2-metadata-token-ttl-seconds': '60'})
    with urllib.request.urlopen(request, timeout=2) as response:
        token = response.read().decode()
    request = urllib.request.Request(base + path, headers={'X-aws-ec2-metadata-token': token})
    with urllib.request.urlopen(request, timeout=2) as response:
        body = response.read().decode()
        return body.strip() if text else json.loads(body)


def data_devices(config):
    """Identify requested disks without formatting, mounting or writing to them."""
    def read(command):
        return subprocess.run(command, check=True, text=True, capture_output=True, timeout=15).stdout

    inventory = json.loads(read(['lsblk', '--json', '--bytes', '--paths', '--output',
                                'NAME,TYPE,SIZE,MODEL,SERIAL,MOUNTPOINTS']))
    disks, roots = {}, set()
    root_found = False

    def visit(node, parents=()):
        nonlocal root_found
        if node['type'] == 'disk':
            parents += (node['name'],)
            disks.setdefault(node['name'], {'device': node['name'], 'size_bytes': int(node['size']),
                'model': (node.get('model') or '').strip(), 'serial': (node.get('serial') or '').strip(),
                'mountpoints': set()})
        mounts = {m for m in node.get('mountpoints', []) if m}
        for parent in parents:
            disks[parent]['mountpoints'].update(mounts)
        if '/' in mounts:
            root_found = bool(parents) or root_found
        if mounts & {'/', '/boot', '/boot/efi'}:
            roots.update(parents)
        for child in node.get('children', []):
            visit(child, parents)

    for node in inventory['blockdevices']:
        visit(node)
    if not root_found:
        raise ValueError('cannot establish root-disk ancestry; refusing data-device discovery')
    for disk in disks.values():
        disk['mountpoints'] = sorted(disk['mountpoints'])
    available = [d for name, d in sorted(disks.items()) if name not in roots]
    result = {'format': 1, 'ebs': {}, 'instance_store': [], 'root_devices': sorted(roots)}

    def attachment(value):
        value = value.strip().removeprefix('/dev/')
        if not re.fullmatch(r'(sd|xvd)[a-z]+', value):
            raise ValueError(f'invalid whole-disk attachment name: {value!r}')
        return '/dev/' + re.sub(r'^xvd', 'sd', value)

    wanted = {attachment(v['attachment']): v for v in config.get('data_volumes', [])}
    volume_ids = set()
    if wanted:
        for disk in available:
            if disk['model'] != 'Amazon Elastic Block Store':
                continue
            # AWS vendor data carries the launch attachment name; NVMe order does not.
            output = read(['ebsnvme-id', disk['device']]).strip().splitlines()
            if len(output) != 2 or not re.fullmatch(r'Volume ID: vol-[0-9a-f]+', output[0]):
                raise ValueError(f'unrecognized ebsnvme-id output for {disk["device"]}: {output!r}')
            label, volume_id = attachment(output[1]), output[0].removeprefix('Volume ID: ')
            if label not in wanted:
                continue
            volume = wanted[label]
            if volume['name'] in result['ebs'] or volume_id in volume_ids:
                raise ValueError(f'ambiguous EBS mapping for {label}')
            if (disk['serial'].replace('-', '') != volume_id.replace('-', '') or
                    disk['size_bytes'] != volume['size_gib'] * 1024**3):
                raise ValueError(f'EBS identity/size mismatch for {label}')
            volume_ids.add(volume_id)
            result['ebs'][volume['name']] = disk | {'volume_id': volume_id, 'attachment': label,
                **{k: volume[k] for k in ('type', 'iops', 'throughput_mib_s') if k in volume}}
        missing = {v['name'] for v in wanted.values()} - result['ebs'].keys()
        if missing:
            raise ValueError(f'requested EBS disks missing or excluded as root: {sorted(missing)}')

    count = config.get('instance_store_count', 0)
    if count and config['instance_store_nvme']:
        result['instance_store'] = [d | {'identity': 'nvme-model-and-serial'} for d in available
            if d['model'] == 'Amazon EC2 NVMe Instance Storage' and d['serial']]
        if len(result['instance_store']) < count:
            raise ValueError(f'expected at least {count} non-root NVMe instance-store disks')
    elif count:
        seen = set()
        for mapping in config['instance_store_mappings']:
            label = attachment(metadata('meta-data/block-device-mapping/' + mapping['name'], text=True))
            if label != attachment(mapping['attachment']):
                raise ValueError(f'instance-store IMDS mapping mismatch for {mapping["name"]}')
            matches = [d for d in available if re.fullmatch(r'/dev/(sd|xvd)[a-z]+', d['device'])
                       and attachment(d['device']) == label]
            if len(matches) != 1 or matches[0]['device'] in seen:
                raise ValueError(f'missing, root or ambiguous instance-store disk: {mapping["name"]}')
            seen.add(matches[0]['device'])
            result['instance_store'].append(matches[0] | {'name': mapping['name'], 'identity': 'imds:' + mapping['name']})
        if len(result['instance_store']) != count:
            raise ValueError('instance-store mapping count mismatch')
    return result


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
        self.job_base = base / 'jobs' / job['id'] if job.get('format', 1) >= 2 else base
        self.results = self.job_base / 'results'
        self.results.mkdir(parents=True, exist_ok=True)
        self.source = base / 'source'
        self.source.mkdir(exist_ok=True)
        self.interrupted = threading.Event()
        self.stop = threading.Event()
        self.collection_seconds = min(120, self.config['deadline_seconds'] // 4)
        self.execution_end = job['created_at'] + self.config['deadline_seconds'] - self.collection_seconds
        self.state = {'state': 'starting', 'job': job['id'], 'worker': job.get('worker')}
        self.process = None
        self.capacity = self.config.get('actual_capacity', self.config['capacity'])

    def aws(self, *args, timeout=45):
        return subprocess.run(['aws', *args, '--region', self.config['region'], '--no-cli-pager'],
                              check=True, stdout=subprocess.DEVNULL, timeout=timeout)

    def upload(self, path, suffix):
        self.aws('s3', 'cp', '--only-show-errors', str(path), self.job['uri'] + '/' + suffix)

    def status(self, state, **fields):
        self.state = self.state | fields | {'state': state, 'updated_at': time.time()}
        path = self.job_base / 'status.json'
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
            if self.job.get('format', 1) >= 2:
                refresh_source(bundle, self.source)
            else:
                extract(bundle, self.source)
            self.aws('s3', 'cp', '--only-show-errors', self.job['uri'] + '/source-manifest.json',
                     str(self.results / 'source-manifest.json'))
            # Existing Run helpers need a Git working tree. This synthetic commit
            # contains exactly the captured files; original HEAD remains in job.json.
            subprocess.run(['git', 'init', '-q'], cwd=self.source, check=True)
            names = [p.name for p in self.source.iterdir() if p.name not in {'build', '.git'}]
            subprocess.run(['git', 'add', '--force', '--', *names], cwd=self.source, check=True)
            subprocess.run(['git', '-c', 'user.name=SixDB snapshot', '-c', 'user.email=snapshot@sixdb.invalid',
                            'commit', '-qm', 'Captured worker source ' + self.job['source']['digest']],
                           cwd=self.source, check=True)
            monitor = threading.Thread(target=self.monitor, daemon=True)
            if self.capacity != 'on-demand' or self.config['sync_seconds']:
                monitor.start()
            else:
                monitor = None
            setup_marker = self.base / 'setup-complete.json'
            setup_id = self.job['source'].get('setup_sha256')
            cached_setup = (self.job.get('worker', {}).get('reused') and setup_marker.exists()
                            and json.loads(setup_marker.read_text()) == {'sha256': setup_id})
            if self.config['setup'] == 'toolchain' and not cached_setup:
                self.status('setting-up')
                code = self.execute(['bash', 'workbench/tools/worker-setup.sh'], self.results / 'setup.log')
                if code:
                    raise RuntimeError(f'toolchain setup exited {code}; see setup.log')
            setup_marker.write_text(json.dumps({'sha256': setup_id}) + '\n')
            host = {'worker': self.job.get('worker'), 'setup_reused': bool(cached_setup)}
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
            env |= {'SIXDB_WORKER_ID': self.job.get('worker', {}).get('id', self.job['id']),
                    'SIXDB_WORKER_REUSED': '1' if self.job.get('worker', {}).get('reused') else '0',
                    'SIXDB_RESULTS': str(self.results), 'SIXDB_SOURCE': str(self.source),
                    'SIXDB_JOB': self.job['id'], 'SIXDB_RESULTS_S3': self.job['uri'] + '/live/',
                    'SIXDB_SOURCE_COMMIT': self.job['source_commit']}
            if self.config.get('data_volumes') or self.config.get('instance_store_count'):
                devices = self.results / 'devices.json'
                devices.write_text(json.dumps(data_devices(self.config), indent=2) + '\n')
                env['SIXDB_DEVICES'] = str(devices)
            self.status('running')
            script_started = time.time()
            command = ['bash', self.job['script'], *self.job['args']]
            if Path('/usr/bin/time').exists():
                command = ['/usr/bin/time', '-q', '-o', str(self.results / 'script.resources'),
                    '-f', 'seconds=%e\nmax_process_rss_kib=%M', *command]
            code = self.execute(command, self.results / 'script.log', env)
            result = {'state': 'complete' if code == 0 else 'failed', 'script_returncode': code,
                      'script_seconds': time.time() - script_started}
            if code:
                result['failure_phase'] = 'running'
            if self.interrupted.is_set():
                result['state'] = 'interrupted'
        except Exception as error:
            result |= {'state': 'interrupted' if isinstance(error, InterruptedError) else
                       'timeout' if isinstance(error, TimeoutError) else 'failed', 'error': str(error),
                       'failure_phase': self.state['state']}
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


def refresh_source(bundle, source):
    """Fresh captured files at stable paths, with unchanged mtimes and build cache."""
    with tempfile.TemporaryDirectory(dir=source.parent) as temporary:
        incoming = Path(temporary) / 'source'
        incoming.mkdir()
        extract(bundle, incoming)
        if (incoming / 'build').exists() or (incoming / '.git').exists():
            raise ValueError('source archive includes reserved build or Git state')
        for path in incoming.rglob('*'):
            old = source / path.relative_to(incoming)
            if path.is_file() and old.is_file() and not old.is_symlink() and path.read_bytes() == old.read_bytes():
                os.utime(path, ns=(old.stat().st_atime_ns, old.stat().st_mtime_ns))
        build = source / 'build'
        if build.is_symlink():
            raise ValueError('cannot reuse a symlinked build directory')
        if build.exists():
            build.rename(incoming / 'build')
        shutil.rmtree(source)
        incoming.rename(source)


class Session:
    """A sequential job loop with an idle-only S3 mailbox and OS shutdown backstops."""
    def __init__(self, job, base):
        import worker_pool
        self.pool = worker_pool
        self.first, self.base = job, base
        self.config = job['config']
        self.id = job['id']
        self.key = worker_pool.state_key(self.id)
        self.store = worker_pool.Store(self.config['bucket'], self.config['region'])
        self.max_end = job['created_at'] + self.config['max_age_seconds']
        self.info = metadata('dynamic/instance-identity/document')
        self.state = {'worker_id': self.id, 'instance_id': self.info['instanceId'],
                      'profile': job['profile'], 'job_id': job['id'], 'state': 'busy',
                      'capacity': self.config['actual_capacity'], 'subnet_id': job['worker']['subnet_id'],
                      'max_end': self.max_end, 'reuse_count': 0}

    def deadline(self, job, active):
        unit = 'sixdb-job-' + job['id']
        if active:
            seconds = max(1, int(min(self.max_end, job['created_at'] + job['config']['deadline_seconds']) - time.time()))
            subprocess.run(['systemd-run', '--collect', '--unit=' + unit, '--on-active=' + str(seconds) + 's',
                            '/sbin/shutdown', '-h', 'now'], check=True)
        else:
            subprocess.run(['systemctl', 'stop', unit + '.timer'], check=True)

    def run_job(self, job, state):
        worker = {'id': self.id, 'instance_id': self.info['instanceId'],
                  'reused': state['reuse_count'] > 0, 'reuse_count': state['reuse_count'],
                  'max_end': self.max_end}
        job = job | {'worker': worker, 'config': job['config'] | {'actual_capacity': state['capacity']}}
        directory = self.base / 'jobs' / job['id']
        directory.mkdir(parents=True, exist_ok=False)
        path = directory / 'job.json'
        path.write_text(json.dumps(job, indent=2) + '\n')
        self.deadline(job, True)
        if state['reuse_count'] == 0:
            subprocess.run(['systemctl', 'stop', 'sixdb-boot-deadline.timer'], check=True)
        # New interpreter per job: imported collection helpers come from this source snapshot.
        subprocess.run([sys.executable, str(Path(__file__).resolve()), '--execute', str(path), str(self.base)], check=False)
        self.deadline(job, False)
        status = directory / 'status.json'
        result = json.loads(status.read_text()) if status.exists() else {}
        return (result.get('state') in {'complete', 'failed'} and result.get('artifact')
                and result.get('script_returncode') is not None)

    def next_job(self):
        while True:
            state, etag = self.store.read(self.key)
            if not state or state['state'] == 'retiring':
                return None
            if state['state'] == 'assigned':
                self.state = state  # Attribute validation failures to this request.
                uri = state['job_uri']
                expected_prefix = f's3://{self.config["bucket"]}/sixdb/workers/{state["job_id"]}'
                if uri != expected_prefix:
                    raise ValueError('assignment URI does not match this worker bucket/job')
                job, _ = self.store.read(f'sixdb/workers/{state["job_id"]}/job.json')
                encoded = (json.dumps(job, indent=2, sort_keys=True) + '\n').encode()
                if (hashlib.sha256(encoded).hexdigest() != state['job_sha256']
                        or job['id'] != state['job_id'] or job['profile'] != self.first['profile']
                        or self.pool.profile(job['config'], job['source']) != self.first['profile']):
                    raise ValueError('assigned job identity/configuration changed')
                busy = state | {'state': 'busy', 'reuse_count': state['reuse_count'] + 1}
                if self.store.replace(self.key, busy, etag):
                    self.state = busy
                    return job
                continue
            if state['state'] != 'idle':
                raise ValueError('unexpected worker mailbox state')
            if time.time() >= min(state['idle_until'], self.max_end):
                if self.store.replace(self.key, state | {'state': 'retiring'}, etag):
                    return None
                continue  # A concurrent assignment may have won just before expiry.
            time.sleep(min(2, max(0, state['idle_until'] - time.time())))

    def run(self):
        try:
            return self.loop()
        except Exception as error:
            # Publish supervisor failures too, including failures before a child
            # can create results. Never replace a completed job's artifact receipt.
            try:
                state, etag = self.store.read(self.key)
                if state and state.get('job_id') == self.state['job_id']:
                    self.store.replace(self.key, state | {'state': 'retiring', 'error': str(error)}, etag)
                key = f'sixdb/workers/{self.state["job_id"]}/status.json'
                status, etag = self.store.read(key)
                if not status or not status.get('artifact'):
                    self.store.replace(key, {'state': 'failed', 'job': self.state['job_id'],
                        'worker': {'id': self.id, 'instance_id': self.info['instanceId']},
                        'error': f'Worker supervisor: {error}', 'updated_at': time.time()}, etag)
            except Exception as publication_error:
                print(f'Supervisor failure could not be published: {publication_error}', flush=True)
            raise

    def loop(self):
        if not self.store.replace(self.key, self.state, None):
            return 1  # For example, cancelled before boot completed.
        job = self.first
        while job is not None:
            reusable = self.run_job(job, self.state)
            state, etag = self.store.read(self.key)
            if not state or state['state'] != 'busy' or state['job_id'] != job['id']:
                return 1
            idle_until = min(time.time() + job['config']['idle_seconds'], self.max_end)
            state = state | {'state': 'idle' if reusable and idle_until > time.time() else 'retiring',
                             'idle_until': idle_until}
            if not self.store.replace(self.key, state, etag) or state['state'] == 'retiring':
                return 0
            job = self.next_job()
        return 0


if __name__ == '__main__':
    if sys.argv[1] == '--execute':
        job_path = Path(sys.argv[2])
        sys.exit(Worker(json.loads(job_path.read_text()), Path(sys.argv[3])).run())
    job_path = Path(sys.argv[1])
    job = json.loads(job_path.read_text())
    if job.get('format', 1) >= 2:
        sys.exit(Session(job, job_path.parent).run())
    sys.exit(Worker(job, job_path.parent).run())
