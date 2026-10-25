#!/usr/bin/env python3
"""Bounded shared immutable inputs with verified archive recovery.

The catalog serializes admission and publication. Persistent pins and registered
borrowers protect accepted bytes; recoverable unpinned content can leave only
after fresh source verification. Public input lifetimes are owned by inputs.py.
"""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import stat
import time
import uuid

DIGEST = re.compile(r'[0-9a-f]{64}\Z')
CHUNK = 65536
MARGIN = 65536  # Directories, journal and atomic metadata publication overlap.


class Refused(Exception):
    pass


class CheckedOutput:
    """A preallocated regular file; no producer may write beyond its allowance."""
    def __init__(self, stream, digest, size):
        self.stream, self.digest, self.size = stream, digest, size
        self.hash, self.copied = hashlib.sha256(), 0

    def write(self, data):
        if len(data) > self.size - self.copied:
            raise Refused('producer exceeded declared object size')
        remaining = memoryview(data)
        while remaining:
            written = self.stream.write(remaining)
            if not written:
                raise Refused('short object write')
            remaining = remaining[written:]
        self.hash.update(data)
        self.copied += len(data)

    def finish(self):
        if self.copied != self.size or self.hash.hexdigest() != self.digest:
            raise Refused('producer bytes differ from declared size/SHA-256')
        self.stream.flush()
        os.fsync(self.stream.fileno())
        os.fchmod(self.stream.fileno(), 0o444)


def digest_file(path, *, dir_fd=None):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=dir_fd)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise Refused('only regular files are supported')
        value = hashlib.sha256()
        for chunk in iter(lambda: stream.read(CHUNK), b''):
            value.update(chunk)
        return value.hexdigest(), info.st_size


def stop_at(name):
    if os.environ.get('SIXDB_STORAGE_TEST_STOP') == name:
        os.kill(os.getpid(), signal.SIGSTOP)


def proc_view():
    info = Path('/proc').stat()
    return f'{info.st_dev}:{info.st_ino}'


def process_identity(pid, *, visible=False):
    """Capture native PIDs in the visible proc view; stored PIDs stay canonical."""
    if type(pid) is not int or pid <= 0:
        raise Refused('invalid borrower PID')
    if not visible:
        if pid == os.getpid():
            pid = int(os.readlink('/proc/self'))
        else:
            # pidfd_open interprets a native PID in the current PID namespace;
            # fdinfo renders it in procfs's namespace. This avoids a racy scan
            # and also handles another native PID under an outer proc mount.
            fd = os.pidfd_open(pid)
            try:
                fields = Path(f'/proc/self/fdinfo/{fd}').read_text().splitlines()
                pid = int(next(line.split(':', 1)[1] for line in fields if line.startswith('Pid:')))
                if pid <= 0:raise ProcessLookupError('borrower ended during capture')
            finally:os.close(fd)
    proc = Path('/proc') / str(pid)
    fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    if fields[0] == 'Z':
        raise ProcessLookupError('borrower is a zombie')
    return {'pid': pid, 'start': fields[19],
            'boot': Path('/proc/sys/kernel/random/boot_id').read_text().strip(), 'proc_root': proc_view()}


def owner_alive(owner):
    try:
        if owner['boot'] != Path('/proc/sys/kernel/random/boot_id').read_text().strip():return False
        if owner.get('proc_root', proc_view()) != proc_view():return True  # Cannot prove death across foreign views.
        current = process_identity(owner['pid'], visible=True)
        return all(current[key] == owner[key] for key in ('pid', 'start', 'boot'))
    except (OSError, ValueError, KeyError, IndexError, Refused):
        return False


def generated_alive(owner):
    return owner_alive(owner) and (not owner.get('acquiring') or owner_alive(owner.get('manager', {})))


class Store:
    def __init__(self, root, budget):
        self.root = Path(root).absolute()
        self.budget = budget
        if budget < MARGIN * 2:
            raise Refused('immutable store budget must be at least 128 KiB')
        if self.root.is_symlink():
            raise Refused('managed root is a symlink')
        self.root.mkdir(parents=True, exist_ok=True)
        self.lock = os.fdopen(os.open(self.root / '.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600), 'a')
        fcntl.flock(self.lock, fcntl.LOCK_EX)
        for name in ['objects', 'staging']:
            path = self.root / name
            if path.is_symlink():
                raise Refused(f'{name} is a symlink')
            path.mkdir(exist_ok=True)
        self.fds = {self.root: os.open(self.root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)}
        for name in ['objects', 'staging']:
            self.fds[self.root / name] = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                                dir_fd=self.fds[self.root])
        self.layout = {name: (self.root / name).stat().st_ino for name in ['objects', 'staging']}
        self.consumer_objects = set()
        self.consumer_keys = set()
        self.generated_registration = {}
        self.catalog_path = self.root / 'catalog.json'
        if self.catalog_path.is_symlink():
            raise Refused('catalog is a symlink')
        if self.catalog_path.exists():
            self.catalog = self.read_json('catalog.json')
            self.validate_catalog()
        else:
            if any((self.root / 'objects').iterdir()):
                raise Refused('missing catalog with existing objects; preserve and investigate')
            self.catalog = {'version': 1, 'budget': budget, 'objects': {}, 'keys': {}, 'leases': {}}
            # Initial ENOSPC/SIGKILL must not expose an empty catalog.json.
            temporary = self.root / ('.catalog-' + uuid.uuid4().hex)
            self.admit(len(self.encode(self.catalog)) + MARGIN)
            self.write_file(temporary, self.encode(self.catalog), exclusive=True)
            self.replace(temporary, self.catalog_path)
            self.sync(self.root)
        self.reconcile()
        self.reap_generated(persist=False)

    def close(self):
        for fd in self.fds.values():
            os.close(fd)
        self.lock.close()

    def validate_catalog(self):
        c = self.catalog
        if c.get('version') != 1 or c.get('budget') != self.budget:
            raise Refused('catalog version/budget differs; do not silently reset it')
        if not all(isinstance(c.get(k), dict) for k in ['objects', 'keys', 'leases']):
            raise Refused('invalid catalog maps')
        for digest, entry in c['objects'].items():
            if (not DIGEST.fullmatch(digest) or not isinstance(entry, dict) or
                    not isinstance(entry.get('bytes'), int) or entry['bytes'] < 0):
                raise Refused('invalid object metadata')
            recovery = entry.get('recovery')
            if recovery is not None:
                self.backend(recovery)
        if any(d not in c['objects'] for d in c['keys'].values()):
            raise Refused('key references unknown object')
        if any(not isinstance(v, list) or any(d not in c['objects'] for d in v)
               for v in c['leases'].values()):
            raise Refused('lease references unknown object')
        if not isinstance(c.get('pin_keys', {}), dict) or any(
                not isinstance(keys, list) or any(not isinstance(key, str) for key in keys)
                for keys in c.get('pin_keys', {}).values()):
            raise Refused('invalid pin key metadata')
        generated = c.get('generated_owners', {})
        if not isinstance(generated, dict) or any(
                not isinstance(token, str) or not token.startswith('input-') or
                not isinstance(owner, dict) or type(owner.get('pid')) is not int or
                not isinstance(owner.get('start'), str) or not isinstance(owner.get('boot'), str) or
                not isinstance(owner.get('view'), str) for token, owner in generated.items()):
            raise Refused('invalid generated borrower ownership')
        for owner in generated.values():
            if 'acquiring' in owner and type(owner['acquiring']) is not bool:
                raise Refused('invalid generated acquisition state')
            manager = owner.get('manager')
            if manager is not None or owner.get('acquiring'):
                if (not isinstance(manager, dict) or type(manager.get('pid')) is not int or
                        not isinstance(manager.get('start'), str) or not isinstance(manager.get('boot'), str)):
                    raise Refused('invalid generated acquisition manager')
        hints = c.get('archive_hints', {})
        if not isinstance(hints, dict) or len(hints) > 64 or any(
                not DIGEST.fullmatch(key) or not isinstance(entry, dict) or
                not isinstance(entry.get('sha256'), str) or not DIGEST.fullmatch(entry['sha256']) or
                type(entry.get('touched')) is not int or entry['touched'] < 0
                for key, entry in hints.items()):
            raise Refused('invalid recent archive proof hints')

    @staticmethod
    def encode(value):
        return (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()

    def backend(self, recovery):
        if isinstance(recovery, dict) and recovery.get('kind') == 'artifact-v1-member':
            from .acquire import ArchiveMemberBackend
            return ArchiveMemberBackend(recovery, self.root, store=self)
        raise Refused('unknown recovery backend')

    def verify_recovery(self, digest, entry):
        self.backend(entry['recovery']).verify(digest, entry['bytes'])

    def sync(self, path):
        os.fsync(self.fds[path])

    def write_file(self, path, data, *, exclusive=False):
        flags = os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | (os.O_EXCL if exclusive else os.O_TRUNC)
        with os.fdopen(os.open(path.name, flags, 0o600, dir_fd=self.fds[path.parent]), 'wb') as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())

    def replace(self, source, destination):
        os.replace(source.name, destination.name, src_dir_fd=self.fds[source.parent],
                   dst_dir_fd=self.fds[destination.parent])

    def unlink(self, path):
        os.unlink(path.name, dir_fd=self.fds[path.parent])

    def read_json(self, name):
        with os.fdopen(os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=self.fds[self.root]), 'r') as inp:
            return json.load(inp)

    def object_digest(self, digest):
        return digest_file(digest, dir_fd=self.fds[self.root / 'objects'])

    def object_exists(self, digest):
        try:
            info = os.stat(digest, dir_fd=self.fds[self.root / 'objects'], follow_symlinks=False)
        except FileNotFoundError:
            return False
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise Refused('object was replaced by a link or non-file; preserved')
        return True

    def check_layout(self):
        for name, inode in self.layout.items():
            p = self.root / name
            if p.is_symlink() or p.stat().st_ino != inode:
                raise Refused(f'managed {name} directory was replaced')

    def used(self):
        self.check_layout()
        total = 0
        for directory, children, names in os.walk(self.root, followlinks=False):
            for path in [Path(directory), *[Path(directory) / n for n in children + names]]:
                if path != Path(directory) and path.is_dir() and not path.is_symlink():
                    continue  # Each directory is charged once when visited.
                info = path.lstat()
                if stat.S_ISLNK(info.st_mode) or (stat.S_ISREG(info.st_mode) and info.st_nlink != 1):
                    raise Refused(f'link in managed store; preserved: {path}')
                total += info.st_blocks * 512
        return total

    def object_path(self, digest):
        if not DIGEST.fullmatch(digest):
            raise Refused('invalid object digest')
        return self.root / 'objects' / digest

    def reconcile(self):
        pending = self.root / 'pending.json'
        if pending.is_symlink():
            raise Refused('pending journal is a symlink')
        journal = self.read_json('pending.json') if pending.exists() else None
        if journal:
            digests = journal.get('digests', [journal.get('digest')])
            if not isinstance(digests, list):
                raise Refused('invalid pending object list')
            for digest in digests:
                if not digest or digest in self.catalog['objects']:
                    continue
                path = self.object_path(digest)
                if self.object_exists(digest):
                    if self.object_digest(digest)[0] != digest:
                        raise Refused('changed unacknowledged object; preserved')
                    self.unlink(path)
        # These directories/names are private unacknowledged transaction scratch.
        for path in (self.root / 'staging').iterdir():
            if not re.fullmatch(r'stage-[0-9a-f]{32}', path.name) or path.is_symlink() or not path.is_file():
                raise Refused(f'unknown staging material; preserved: {path}')
            self.unlink(path)
        for path in self.root.glob('.catalog-*'):
            if not re.fullmatch(r'\.catalog-[0-9a-f]{32}', path.name) or path.is_symlink() or not path.is_file():
                raise Refused('unknown catalog staging; preserved')
            self.unlink(path)
        for path in (self.root / 'objects').iterdir():
            if path.is_symlink() or path.name not in self.catalog['objects']:
                raise Refused(f'unknown managed object; preserved: {path}')
        # The journal must outlive the durable removals it explains. An absent
        # journal must not be persisted before object/staging cleanup is synced.
        self.sync(self.root / 'staging')
        self.sync(self.root / 'objects')
        if pending.exists():
            self.unlink(pending)
        self.sync(self.root)

    def reap_generated(self, *, persist=True):
        future = json.loads(json.dumps(self.catalog))
        owners = future.get('generated_owners', {})
        for token, owner in list(owners.items()):
            if not generated_alive(owner):
                future['leases'].pop(token, None)
                future.get('pin_keys', {}).pop(token, None)
                del owners[token]
        if future != self.catalog:
            self.catalog = future  # Admission may reclaim already-dead owners.
            if persist:
                self.commit(future)

    def admit(self, extra, protected=(), *, inodes=4, external=False):
        self.reap_generated(persist=False)
        protected = set(protected) | self.consumer_objects | {d for ds in self.catalog['leases'].values() for d in ds}
        reasons = []
        import storage
        configured = storage.configuration() is not None
        def capacity():
            fs = os.fstatvfs(self.fds[self.root])
            free = fs.f_bavail * fs.f_frsize
            if configured:
                free = min(free, storage.headroom(self.root)['destination']['free'])
            return fs, free
        def physical_room():
            fs, free = capacity()
            return free >= extra and (not fs.f_files or fs.f_favail >= inodes)
        while self.used() + (0 if external else extra) > self.budget or not physical_room():
            progress = False
            for digest, entry in sorted(self.catalog['objects'].items(), key=lambda item: item[1]['touched']):
                path = self.object_path(digest)
                if not self.object_exists(digest) or digest in protected or not entry.get('recovery'):
                    continue
                try:
                    if self.object_digest(digest) != (digest, entry['bytes']):
                        raise Refused('local cached bytes changed')
                    self.verify_recovery(digest, entry)
                except (OSError, Refused) as error:
                    reasons.append(f'{digest}: {error}')
                    continue
                self.check_layout()
                self.unlink(path)
                self.sync(path.parent)
                progress = True
                break
            if not progress:
                fs, free = capacity()
                raise Refused(f'capacity refused: {self.used()} allocated + {extra} reserved, budget {self.budget}; '
                              f'filesystem/quota free={free}, free inodes={fs.f_favail}; '
                              f'leases={list(self.catalog["leases"])}; unique/changed/unrecoverable data preserved; '
                              + '; '.join(reasons[-3:]))

    def trim_metadata(self, keep_keys=()):
        """Recoverable cache aliases are not a permanent historical index."""
        limit = max(4096, self.budget // 16)
        if len(self.encode(self.catalog)) <= limit:
            return
        future = json.loads(json.dumps(self.catalog))
        # Membership hints do not own payload or recovery truth. Retire them
        # before deleting useful cache aliases when metadata itself is crowded.
        hints = future.get('archive_hints', {})
        for key in sorted(hints, key=lambda key: hints[key]['touched']):
            if len(self.encode(future)) <= limit // 2:
                break
            del hints[key]
        pinned = {d for ds in future['leases'].values() for d in ds} | self.consumer_objects
        keep_keys = set(keep_keys) | self.consumer_keys | {
            key for keys in future.get('pin_keys', {}).values() for key in keys}
        keep_objects = pinned | {future['keys'][key] for key in keep_keys if key in future['keys']}
        verified = {}
        def removable(digest):
            if digest not in verified:
                entry = future['objects'][digest]
                try:
                    verified[digest] = bool(entry.get('recovery')) and (
                        not self.object_exists(digest) or self.object_digest(digest) == (digest, entry['bytes']))
                    if verified[digest]:
                        self.verify_recovery(digest, entry)
                except (OSError, Refused):
                    verified[digest] = False
            return verified[digest]
        def retire_orphans():
            referenced = set(future['keys'].values()) | keep_objects
            for digest in list(future['objects']):
                if digest not in referenced and removable(digest):
                    if self.object_exists(digest):
                        self.unlink(self.object_path(digest))
                        self.sync(self.root / 'objects')
                    del future['objects'][digest]
        retire_orphans()
        for key in sorted(list(future['keys']), key=lambda k: future['objects'][future['keys'][k]]['touched']):
            if len(self.encode(future)) <= limit // 2:
                break
            digest = future['keys'][key]
            if key not in keep_keys and removable(digest):
                del future['keys'][key]
                retire_orphans()
        if future != self.catalog:
            self.commit(future, protected=keep_objects)

    def commit(self, catalog, *, protected=(), digests=()):
        # A caller can hold its candidate snapshot across a slow transfer while
        # another borrower ends. Never republish stale generated ownership from
        # that earlier snapshot after admission has already reaped it.
        for token, owner in list(catalog.get('generated_owners', {}).items()):
            if not generated_alive(owner):
                catalog['leases'].pop(token, None)
                catalog.get('pin_keys', {}).pop(token, None)
                del catalog['generated_owners'][token]
        data = self.encode(catalog)
        self.admit(len(data) + MARGIN, protected)
        # Atomic journal visibility means a killed journal writer cannot publish
        # a partial pending.json. Its recognizable temporary is restart scratch.
        journal = self.root / ('.catalog-' + uuid.uuid4().hex)
        self.write_file(journal, self.encode({'digests': list(digests)}), exclusive=True)
        self.replace(journal, self.root / 'pending.json')
        self.sync(self.root)
        temporary = self.root / ('.catalog-' + uuid.uuid4().hex)
        self.write_file(temporary, data, exclusive=True)
        if self.used() > self.budget:
            raise Refused('actual metadata allocation exceeded reservation; no publication')
        self.replace(temporary, self.catalog_path)
        self.sync(self.root)
        self.catalog = catalog
        stop_at('after-catalog')
        self.unlink(self.root / 'pending.json')
        self.sync(self.root)

    def add_archive_hint(self, catalog, key, proof):
        if not DIGEST.fullmatch(key) or not DIGEST.fullmatch(proof):
            raise Refused('invalid archive proof identity')
        hints = catalog.setdefault('archive_hints', {})
        hints[key] = {'sha256': proof, 'touched': time.time_ns()}
        limit = max(512, min(8192, self.budget // 128))
        while len(hints) > 64 or len(self.encode(hints)) > limit:
            oldest = min((name for name in hints if name != key), key=lambda name: hints[name]['touched'])
            del hints[oldest]

    def remember_archive_hint(self, key, proof):
        self.trim_metadata()
        future = json.loads(json.dumps(self.catalog))
        self.add_archive_hint(future, key, proof)
        self.commit(future)

    def ingest_records(self, records, produce, lease=None, *, archive_hint=None):
        """One bounded transaction for a verified stream of selected objects.

        `produce` receives digest -> CheckedOutput for missing objects, and must
        validate its complete source before returning. No object is published
        until all outputs and the complete source have passed verification.
        """
        if not records or (lease is not None and len(lease) > 128):
            raise Refused('records must be nonempty; pin labels are at most 128 characters')
        keys, by_digest = [], {}
        for record in records:
            key, digest, size = record['key'], record['sha256'], record['bytes']
            if not isinstance(key, str) or len(key) > 256 or not DIGEST.fullmatch(digest) or type(size) is not int or size < 0:
                raise Refused('invalid acquisition record')
            if record.get('recovery') is not None:
                self.backend(record['recovery'])
            if digest in by_digest and by_digest[digest]['bytes'] != size:
                raise Refused('inconsistent size for one content digest')
            by_digest[digest] = record
            keys.append(key)
        if len(set(keys)) != len(keys):
            raise Refused('duplicate acquisition keys')
        self.trim_metadata(keys)
        future = json.loads(json.dumps(self.catalog))
        if archive_hint is not None:
            self.add_archive_hint(future, *archive_hint)
        missing = {}
        for digest, record in by_digest.items():
            size = record['bytes']
            exists = self.object_exists(digest)
            if exists and self.object_digest(digest) != (digest, size):
                raise Refused('cached object differs; preserved')
            if not exists:
                missing[digest] = size
            future['objects'].setdefault(digest, {'bytes': size, 'recovery': record.get('recovery'), 'touched': 0})
            entry = future['objects'][digest]
            if record.get('recovery') is None:
                entry['recovery'] = None
            elif entry['recovery'] is not None:
                entry['recovery'] = record['recovery']
            entry['touched'] = time.time_ns()
        for record in records:
            future['keys'][record['key']] = record['sha256']
        if lease is not None:
            ds = future['leases'].setdefault(lease, [])
            ks = future.setdefault('pin_keys', {}).setdefault(lease, [])
            ds.extend(d for d in by_digest if d not in ds)
            ks.extend(key for key in keys if key not in ks)
            if lease in self.generated_registration:
                future.setdefault('generated_owners', {})[lease] = self.generated_registration[lease]
        # Reserve the whole selected working set (not the archive's compressed
        # copy), metadata overlap and directory allowance before any data write.
        extra = sum(((size + 4095) // 4096) * 4096 for size in missing.values()) + len(self.encode(future)) * 2 + MARGIN
        self.admit(extra, by_digest, inodes=len(missing) + 4)
        journal = self.root / ('.catalog-' + uuid.uuid4().hex)
        self.write_file(journal, self.encode({'digests': list(missing)}), exclusive=True)
        self.replace(journal, self.root / 'pending.json')
        self.sync(self.root)
        stages, streams, outputs = {}, {}, {}
        try:
            for digest, size in missing.items():
                stage = self.root / 'staging' / ('stage-' + uuid.uuid4().hex)
                fd = os.open(stage.name, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                             0o600, dir_fd=self.fds[stage.parent])
                stream = os.fdopen(fd, 'r+b', buffering=0)
                stages[digest], streams[digest] = stage, stream
                # Native allocation reserves the exact payload before a producer
                # sees a writer. ENOSPC is handled before streaming into it.
                if size:
                    os.posix_fallocate(fd, 0, size)
                outputs[digest] = CheckedOutput(stream, digest, size)
            if self.used() > self.budget:
                raise Refused('filesystem allocation exceeded admission estimate before streaming')
            produce(outputs)
            for output in outputs.values():
                output.finish()
            stop_at('after-stage')
        finally:
            for stream in streams.values():
                stream.close()
        self.check_layout()
        for digest, stage in stages.items():
            self.replace(stage, self.object_path(digest))
        self.sync(self.root / 'objects')
        stop_at('after-object')
        self.commit(future, protected=by_digest, digests=missing)

    def release(self, owner):
        future = json.loads(json.dumps(self.catalog))
        future['leases'].pop(owner, None)
        future.get('pin_keys', {}).pop(owner, None)
        future.get('generated_owners', {}).pop(owner, None)
        self.catalog = future  # Released recoverable bytes can fund metadata.
        self.commit(future)
