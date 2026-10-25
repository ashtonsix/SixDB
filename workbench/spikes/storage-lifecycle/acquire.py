#!/usr/bin/env python3
"""Managed v1 artifact acquisition with reopenable local/S3 byte streams.

Default acquisition omits source packages and large compiler output. `run` is
normal consumption: SIXDB_INPUTS_FD carries the member-name/CAS-path mapping.
An unscoped acquire returns identities, not paths; --pin is an explicit durable
pin requiring release. No extracted per-reference tree is created.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import select
import signal
import stat
import subprocess
import sys
import tarfile
import threading
import zlib

from store import CHUNK, DIGEST, Refused, Store

MAX_REFERENCE = 16384
MAX_NAME = 1024
MAX_FILES = 8192  # Existing compact recovery map includes 4,918-member jobs.
DEFAULT_EXPANDED = None  # Automatic finite work allowance, independent of disk admission.
MIN_AUTO_EXPANDED = 64 * 1024 * 1024
AUTO_EXPANSION_RATIO = 8
MAX_STREAM_BYTES = (1 << 63) - 1
MAX_PAX = 65536


def member_name(value):
    if (not isinstance(value, str) or not value or len(value.encode('utf-8')) > MAX_NAME or
            '\0' in value or value.startswith('/') or
            any(part in ('', '.', '..') for part in value.split('/'))):
        raise Refused(f'unsafe/noncanonical archive member: {value!r}')
    return value


def reference_identity(value):
    if not isinstance(value, dict) or type(value.get('format')) is not int or value['format'] != 1:
        raise Refused('expected an existing format-1 artifact reference')
    if (not isinstance(value.get('sha256'), str) or not DIGEST.fullmatch(value['sha256']) or
            type(value.get('bytes')) is not int or value['bytes'] <= 0 or
            type(value.get('files')) is not int or not 1 <= value['files'] <= MAX_FILES):
        raise Refused('invalid v1 compressed SHA/bytes/file count')
    result = {key: value[key] for key in ('format', 'sha256', 'bytes', 'files')}
    for key in ('uri', 'bucket', 'region', 'key', 'kind', 'subdirectory'):
        if key in value:
            if not isinstance(value[key], str) or len(value[key].encode('utf-8')) > 4096:
                raise Refused('invalid reference provenance')
            result[key] = value[key]
    if result.get('subdirectory'):
        member_name(result['subdirectory'])
    if len(Store.encode(result)) > MAX_REFERENCE:
        raise Refused('reference metadata too large')
    return result


def read_json(path, limit):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise Refused('metadata must be a bounded regular file')
        data = stream.read(limit + 1)
        if len(data) > limit:
            raise Refused('metadata grew beyond its limit')
    return json.loads(data)


def manifest_members(value):
    """Distinguish an inspect envelope from an ordinary file named members."""
    if not isinstance(value, dict):
        raise Refused('invalid member manifest')
    candidate = value.get('members')
    # Both the envelope and the plain map are accepted. A file entry has a
    # scalar byte count; a map containing a file named bytes has a dict there.
    if (set(value) <= {'artifact', 'members'} and isinstance(candidate, dict) and
            ('bytes' not in candidate or isinstance(candidate['bytes'], dict))):
        return candidate
    return value


def validate_manifest(value, *, policy=False):
    value = manifest_members(value)
    if not isinstance(value, dict) or len(value) > MAX_FILES:
        raise Refused('invalid member manifest')
    result = {}
    for name, item in value.items():
        member_name(name)
        if (not isinstance(item, dict) or type(item.get('bytes')) is not int or not 0 <= item['bytes'] <= MAX_STREAM_BYTES or
                not isinstance(item.get('sha256'), str) or not DIGEST.fullmatch(item['sha256'])):
            raise Refused('invalid member size/SHA-256')
        result[name] = {key: item[key] for key in ('bytes', 'sha256')}
        if policy or 'rebuildable' in item:
            if type(item.get('rebuildable')) is not bool:
                raise Refused('member selection policy must be a boolean')
            result[name]['rebuildable'] = item['rebuildable']
    return result


def expansion_allowance(reference, configured=None, *, budget=0, expected=None):
    """A finite decompression-work ceiling, not a retained-disk reservation.

    Without an explicit cap, use compressed transport size, managed capacity and
    any full caller-supplied expanded-size declaration. Known sizes include the
    parser's bounded worst-case per-member PAX/header overhead. No omitted bytes
    are materialized to make this allowance fit the cache.
    """
    if configured is not None:
        if type(configured) is not int or not 1024 <= configured <= MAX_STREAM_BYTES:
            raise Refused('invalid expanded-stream allowance')
        return configured
    if type(budget) is not int or not 0 <= budget <= MAX_STREAM_BYTES:
        raise Refused('invalid capacity for automatic stream allowance')
    known = 0
    if expected is not None:
        manifest = validate_manifest(expected)
        if len(manifest) != reference['files']:
            raise Refused('expected full member count differs from reference')
        known = sum(((item['bytes'] + 511) // 512) * 512 for item in manifest.values())
        known += len(manifest) * (512 + MAX_PAX + 512) + 10240
    limit = max(MIN_AUTO_EXPANDED, reference['bytes'] * AUTO_EXPANSION_RATIO, budget, known)
    if limit > MAX_STREAM_BYTES:
        raise Refused('automatic stream allowance exceeds supported bound; choose an explicit finite cap')
    return limit


class LocalArchiveSource:
    def __init__(self, path):
        self.path = Path(path).absolute()

    def descriptor(self):
        return {'kind': 'local', 'path': str(self.path)}

    def validate_root(self, root):
        if self.path.resolve().is_relative_to(root.resolve()):
            raise Refused('archive backend must be independent of managed store')

    @contextmanager
    def open(self, reference, *, store=None):
        if store is not None:
            self.validate_root(store.root)
        fd = os.open(self.path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, 'rb') as raw:
            before = os.fstat(raw.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size != reference['bytes']:
                raise Refused('archive backend type/size differs from reference')
            yield raw
            after = os.fstat(raw.fileno())
            fields = lambda info: (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
            if fields(before) != fields(after):
                raise Refused('archive changed while being read')


class S3ArchiveSource:
    """Reopen the existing v1 immutable object; never stage compressed bytes."""
    STDERR_LIMIT = 65536

    def __init__(self, reference, *, seconds=300):
        self.reference = reference_identity(reference)
        for field in ('bucket', 'key', 'region'):
            value = self.reference.get(field)
            if not isinstance(value, str) or not value or any(c in value for c in '\0\r\n'):
                raise Refused('S3 source needs valid v1 bucket/key/region')
        if type(seconds) is not int or not 1 <= seconds <= 3600:
            raise Refused('S3 stream timeout must be 1..3600 seconds')
        self.seconds = seconds

    def descriptor(self):
        return {'kind': 's3', 'reference': self.reference, 'seconds': self.seconds}

    def validate_root(self, root):
        pass  # Remote object identity is independent of the local namespace.

    @contextmanager
    def open(self, reference, *, store=None):
        core = lambda ref: tuple(ref.get(key) for key in ('format', 'sha256', 'bytes', 'files'))
        if core(reference) != core(self.reference):
            raise Refused('S3 source compressed identity differs from requested artifact')
        command = ['aws', 's3', 'cp', '--only-show-errors',
                   f"s3://{self.reference['bucket']}/{self.reference['key']}", '-',
                   '--region', self.reference['region']]
        guarded = store is not None and store.outer_guard
        if not guarded:
            command = ['unshare', '--user', '--map-root-user', '--mount', '--pid',
                       '--fork', '--mount-proc', '--kill-child=SIGKILL', '--', *command]
        env = dict(os.environ, AWS_PAGER='', AWS_CLI_AUTO_PROMPT='off')
        if store is not None:
            env['TMPDIR'] = str(store.root / 'staging')
        parent = os.getpid()
        libc = ctypes.CDLL(None, use_errno=True)
        def bind_parent_lifetime():
            if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or os.getppid() != parent:
                os._exit(125)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=True, env=env, preexec_fn=bind_parent_lifetime)
        errors, overflow, timed_out, finished = bytearray(), threading.Event(), threading.Event(), threading.Event()
        os.set_blocking(process.stdout.fileno(), False)
        os.set_blocking(process.stderr.fileno(), False)
        def kill():
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        def drain():
            while True:
                ready = select.select([process.stderr], [], [], 0.1)[0]
                if not ready:
                    if finished.is_set():
                        return
                    continue
                try:
                    data = os.read(process.stderr.fileno(), 8192)
                except BlockingIOError:
                    continue
                if not data:
                    return
                available = self.STDERR_LIMIT - len(errors)
                errors.extend(data[:max(0, available)])
                if len(data) > available:
                    overflow.set()
                    kill()
                    return
        def expired():
            timed_out.set()
            kill()
        class Stream:
            def read(self, size=-1):
                if size == 0:
                    return b''
                while True:
                    if timed_out.is_set() or overflow.is_set():
                        raise Refused('S3 stream interrupted by bounded transport supervisor')
                    if not select.select([process.stdout], [], [], 0.1)[0]:
                        continue
                    try:
                        return os.read(process.stdout.fileno(), CHUNK if size < 0 else min(size, CHUNK))
                    except BlockingIOError:
                        continue
        reader = threading.Thread(target=drain, daemon=True)
        watchdog = threading.Timer(self.seconds, expired)
        failure, code = None, None
        reader.start()
        watchdog.start()
        try:
            yield Stream()
            code = process.wait()
        except BaseException as error:
            failure = error
        finally:
            watchdog.cancel()
            if process.poll() is None:
                kill()
            process.wait()
            process.stdout.close()
            finished.set()
            reader.join(timeout=5)
            process.stderr.close()
        if isinstance(failure, (KeyboardInterrupt, SystemExit)):
            raise failure
        if timed_out.is_set():
            raise Refused('S3 stream exceeded its finite time allowance')
        if overflow.is_set() or reader.is_alive():
            raise Refused('S3 transport stderr exceeded its bounded allowance')
        code = process.returncode if code is None else code
        if code != 0:
            detail = bytes(errors[-4096:]).decode('utf-8', errors='replace').strip()
            raise Refused(f'S3 transport failed ({process.returncode}): {detail}') from failure
        if failure is not None:
            raise failure


def archive_source(source, reference):
    if source is None:
        return S3ArchiveSource(reference)
    if isinstance(source, (str, Path)):
        return LocalArchiveSource(source)
    if not isinstance(source, (LocalArchiveSource, S3ArchiveSource)):
        raise Refused('unsupported archive source')
    return source


def source_from_descriptor(value, reference):
    if not isinstance(value, dict):
        raise Refused('invalid recovery source descriptor')
    if value.get('kind') == 'local' and isinstance(value.get('path'), str):
        return LocalArchiveSource(value['path'])
    if value.get('kind') == 's3':
        return S3ArchiveSource(value.get('reference', reference), seconds=value.get('seconds', 300))
    raise Refused('unknown recovery source descriptor')


class CompressedReader:
    def __init__(self, stream, expected):
        self.stream, self.expected = stream, expected
        self.hash, self.count = hashlib.sha256(), 0

    def read(self, size=-1):
        # gzip normally requests a small buffer; never let a parser request an
        # unbounded whole compressed object into memory.
        if size < 0:
            size = CHUNK
        data = self.stream.read(min(size, CHUNK))
        self.count += len(data)
        if self.count > self.expected:
            raise Refused('compressed stream exceeds reference size')
        self.hash.update(data)
        return data


class ExpandedReader:
    def __init__(self, stream, limit):
        self.stream, self.limit, self.count = stream, limit, 0

    def read(self, size):
        data = self.stream.read(min(size, CHUNK))
        self.count += len(data)
        if self.count > self.limit:
            raise Refused('archive expansion exceeds configured stream limit')
        return data

    def exact(self, size):
        if size < 0 or size > self.limit - self.count:
            raise Refused('invalid or oversized tar field')
        data = bytearray()
        while len(data) < size:
            block = self.read(size - len(data))
            if not block:
                raise Refused('truncated tar stream')
            data.extend(block)
        return bytes(data)


def pax_fields(data):
    fields = {}
    while data:
        space = data.find(b' ')
        if space < 1 or not data[:space].isdigit():
            raise Refused('malformed PAX length')
        length = int(data[:space])
        if not space + 3 <= length <= len(data):
            raise Refused('invalid PAX record length')
        record, data = data[space + 1:length], data[length:]
        if not record.endswith(b'\n') or b'=' not in record:
            raise Refused('malformed PAX record')
        key, value = record[:-1].decode('utf-8').split('=', 1)
        if key in fields or key not in {'path', 'size', 'mtime', 'atime', 'ctime', 'uid', 'gid', 'uname', 'gname'}:
            raise Refused('unsupported/duplicate PAX field')
        fields[key] = value
    return fields


def compiler_output(name, size, prefix):
    return size >= 1024 * 1024 and (Path(name).suffix in ('.asm', '.s') or
        prefix.startswith((b'\x7fELF', b'!<arch>\n', b'BC\xc0\xde')) or
        (b'file format elf' in prefix and b'Disassembly of section ' in prefix))


def scan_archive(path, reference, *, outputs=None, expected=None, max_expanded=DEFAULT_EXPANDED, store=None):
    """Hash compressed bytes and every regular member; never extract a tree.

    Small plain/PAX tar is parsed in bounded blocks. Links, sparse files,
    directories, global/GNU extensions, duplicate/noncanonical names and
    nonzero data after the tar terminator are rejected, including omitted data.
    Existing artifacts.pack writes this regular/PAX subset. Accepted output is
    staged by Store and not published until this entire function succeeds.
    """
    reference = reference_identity(reference)
    max_expanded = expansion_allowance(reference, max_expanded, expected=expected)
    outputs = outputs or {}
    if expected is not None:
        expected = validate_manifest(expected)
    source = archive_source(path, reference)
    with source.open(reference, store=store) as raw:
        compressed = CompressedReader(raw, reference['bytes'])
        with gzip.GzipFile(fileobj=compressed, mode='rb') as unzip:
            stream = ExpandedReader(unzip, max_expanded)
            members, parents, overrides = {}, set(), None
            while True:
                header = stream.exact(512)
                if header == bytes(512):
                    if overrides is not None or stream.exact(512) != bytes(512):
                        raise Refused('invalid tar terminator or dangling PAX header')
                    while tail := stream.read(CHUNK):
                        if any(tail):
                            raise Refused('nonzero trailing tar data')
                    if stream.count % 512:
                        raise Refused('misaligned tar trailer')
                    break
                try:
                    info = tarfile.TarInfo.frombuf(header, 'utf-8', 'strict')
                except (tarfile.TarError, UnicodeError, ValueError) as error:
                    raise Refused(f'invalid tar header: {error}') from error
                if info.type == tarfile.XHDTYPE:
                    if overrides is not None or not 0 <= info.size <= MAX_PAX:
                        raise Refused('stacked/oversized PAX header')
                    overrides = pax_fields(stream.exact(info.size))
                    padding = stream.exact((-info.size) % 512)
                    if any(padding):
                        raise Refused('nonzero tar padding')
                    continue
                if info.type not in (tarfile.REGTYPE, tarfile.AREGTYPE) or info.sparse is not None or info.linkname:
                    raise Refused('archive supports only regular files; links/sparse/extensions refused')
                name = member_name((overrides or {}).get('path', info.name))
                size = (overrides or {}).get('size', info.size)
                overrides = None
                if isinstance(size, str):
                    if not size.isascii() or not size.isdigit() or len(size) > 20:
                        raise Refused('invalid PAX member size')
                    size = int(size)
                if type(size) is not int or size < 0 or size > max_expanded - stream.count:
                    raise Refused('member exceeds expanded-stream allowance')
                ancestors = set(str(p) for p in PurePosixPath(name).parents if str(p) != '.')
                if name in members or name in parents or ancestors.intersection(members):
                    raise Refused('duplicate member or file/parent name conflict')
                if len(members) >= reference['files']:
                    raise Refused('too many archive members')
                parents.update(ancestors)
                hashed, copied, prefix = hashlib.sha256(), 0, b''
                output = outputs.get(name)
                while copied < size:
                    chunk = stream.exact(min(CHUNK, size - copied))
                    if not prefix:
                        prefix = chunk[:512]
                    hashed.update(chunk)
                    copied += len(chunk)
                    if output is not None:
                        output.write(chunk)
                if any(stream.exact((-size) % 512)):
                    raise Refused('nonzero member padding')
                member = {'bytes': copied, 'sha256': hashed.hexdigest(),
                          'rebuildable': compiler_output(name, size, prefix)}
                if expected is not None:
                    wanted = expected.get(name)
                    if wanted is None or any(member[key] != value for key, value in wanted.items()):
                        raise Refused(f'member size/SHA-256/selection-policy mismatch: {name}')
                members[name] = member
            if expected is not None and members.keys() != expected.keys():
                raise Refused('member manifest names differ from archive')
        # The gzip EOF read verifies CRC/ISIZE and all concatenated gzip data.
        # Every compressed byte is hashed, including gzip headers and padding.
        while compressed.read(CHUNK):
            pass
        if (compressed.count != reference['bytes'] or compressed.hash.hexdigest() != reference['sha256'] or
                len(members) != reference['files']):
            raise Refused('archive compressed size/SHA-256/file count mismatch')
    return members


class ArchiveMemberBackend:
    def __init__(self, description, root, *, store=None):
        self.description, self.store = description, store
        self.reference = reference_identity(description['reference'])
        self.source = (source_from_descriptor(description['source'], self.reference) if 'source' in description
                       else LocalArchiveSource(description['archive']))
        self.source.validate_root(root)
        self.member = member_name(description['member'])
        self.size, self.digest = description['bytes'], description['sha256']
        self.limit = expansion_allowance(self.reference, description.get('max_expanded'))
        if (type(self.size) is not int or self.size < 0 or not isinstance(self.digest, str) or
                not DIGEST.fullmatch(self.digest)):
            raise Refused('invalid archive recovery descriptor')

    def verify(self, digest, size):
        self.write(None, digest, size)

    def write(self, output, digest, size):
        if (digest, size) != (self.digest, self.size):
            raise Refused('recovery descriptor identity differs')
        try:
            members = scan_archive(self.source, self.reference,
                                   outputs={self.member: output} if output is not None else {},
                                   max_expanded=self.limit, store=self.store)
        except (EOFError, ValueError, tarfile.TarError, zlib.error) as error:
            raise Refused(f'archive recovery failed verification: {error}') from error
        if members.get(self.member, {}).get('sha256') != digest or members[self.member]['bytes'] != size:
            raise Refused('archived member differs from accepted object')


def manifest_digest(members):
    return hashlib.sha256(Store.encode(validate_manifest(members, policy=True))).hexdigest()


def recovery_descriptor(path, reference, name, member, limit, members, *, proof=None):
    source = archive_source(path, reference)
    return {'kind': 'artifact-v1-member', 'source': source.descriptor(),
            'reference': reference, 'member': name, 'bytes': member['bytes'],
            'sha256': member['sha256'], 'max_expanded': limit,
            'manifest_sha256': proof or manifest_digest(members), 'manifest_policy': 1}


def archive_identity(reference):
    return hashlib.sha256(Store.encode({key: reference[key] for key in ('format', 'sha256', 'bytes', 'files')})).hexdigest()


def inspect_archive(reference, source=None, *, store=None, budget=0,
                    max_expanded=DEFAULT_EXPANDED, expected=None):
    reference = reference_identity(reference)
    if (expected is not None and manifest_members(expected) is not expected and 'artifact' in expected and
            reference_identity(expected['artifact']) != reference):
        raise Refused('manifest artifact identity differs')
    limit = expansion_allowance(reference, max_expanded,
                                budget=store.budget if store is not None else budget, expected=expected)
    return {'artifact': reference, 'members': scan_archive(source, reference, expected=expected,
                max_expanded=limit, store=store)}


def scoped_members(reference, members):
    prefix = reference.get('subdirectory', '')
    if not prefix:
        return dict(members)
    prefix += '/'
    result = {name[len(prefix):]: item for name, item in members.items() if name.startswith(prefix)}
    if not result:
        raise Refused('reference subdirectory has no members')
    return result


def choose_members(reference, members, selected, full):
    scoped = scoped_members(reference, members)
    if selected is not None:
        wanted = set(member_name(name) for name in selected)
        if wanted - scoped.keys():
            raise Refused('selected archive members are missing: ' + ', '.join(sorted(wanted - scoped.keys())))
    else:
        wanted = {name for name, item in scoped.items() if full or not (
            Path(name).name in ('source.tar.gz', 'source.tar.zst') or item.get('rebuildable', False))}
    return {name: scoped[name] for name in sorted(wanted)}, sorted(scoped.keys() - wanted)


def acquire(store, reference, source=None, *, selected=None, full=False, pin=None,
            max_expanded=DEFAULT_EXPANDED, expected=None, consumer=False):
    reference = reference_identity(reference)
    max_expanded = expansion_allowance(reference, max_expanded, budget=store.budget, expected=expected)
    path = archive_source(source, reference)
    path.validate_root(store.root)
    cached, members, scanned = False, None, False
    hint_key = archive_identity(reference)
    if expected is not None:
        checked = validate_manifest(expected)
        tentative = manifest_members(expected)
        chosen, omitted = choose_members(reference, tentative, selected, full)
        try:
            proof = manifest_digest(checked)
        except Refused:
            proof = None  # Size/hash-only manifests require policy discovery.
        has_hint = proof is not None and store.catalog.get('archive_hints', {}).get(hint_key, {}).get('sha256') == proof
        core = lambda r: tuple(r.get(k) for k in ('format', 'sha256', 'bytes', 'files'))
        cached = bool(chosen) and proof is not None
        checked_digests = set()
        for item in chosen.values():
            digest = item['sha256']
            entry = store.catalog['objects'].get(digest, {})
            if digest in checked_digests:
                if entry.get('bytes') != item['bytes']:
                    cached = False
                    break
                continue
            recovery = entry.get('recovery')
            legacy_hint = (isinstance(recovery, dict) and recovery.get('manifest_policy') == 1 and
                           recovery.get('manifest_sha256') == proof and
                           core(recovery.get('reference', {})) == core(reference))
            if (entry.get('bytes') != item['bytes'] or not (has_hint or legacy_hint) or
                    not store.object_exists(digest) or store.object_digest(digest) != (digest, item['bytes'])):
                cached = False
                break
            checked_digests.add(digest)
        if cached or proof is not None:
            # A complete declared policy map gives bounded selected lengths. On
            # a miss, one stream checks every declared size/hash/policy before
            # the transaction publishes either payload or the new hint.
            members = checked
    if members is None:
        members = scan_archive(path, reference, expected=expected, max_expanded=max_expanded, store=store)
        scanned = True
    chosen, omitted = choose_members(reference, members, selected, full)
    proof = manifest_digest(members)
    records, digest_names = {}, {}
    prefix = reference.get('subdirectory', '')
    for name, item in chosen.items():
        digest = item['sha256']
        full_name = prefix + '/' + name if prefix else name
        if digest not in records:
            recovery = store.catalog['objects'][digest]['recovery'] if cached else recovery_descriptor(
                path, reference, full_name, item, max_expanded, members, proof=proof)
            records[digest] = {'key': 'member:' + digest, 'sha256': digest, 'bytes': item['bytes'],
                               'recovery': recovery}
            digest_names[digest] = full_name
    if consumer:
        store.consumer_objects.update(records)
        store.consumer_keys.update(item['key'] for item in records.values())
    def produce(outputs):
        if cached:
            if outputs:
                raise Refused('cached member disappeared during acquisition')
            return
        scan_archive(path, reference,
                     outputs={digest_names[digest]: output for digest, output in outputs.items()},
                     expected=members, max_expanded=max_expanded, store=store)
    if records:
        store.ingest_records(list(records.values()), produce, pin, archive_hint=(hint_key, proof))
    else:
        if not scanned:
            scan_archive(path, reference, expected=members, max_expanded=max_expanded, store=store)
        store.remember_archive_hint(hint_key, proof)
    result = {'artifact': reference, 'members': {
        name: {key: item[key] for key in ('bytes', 'sha256')} for name, item in chosen.items()},
        'omitted': omitted, 'pin': pin, 'cache_hit': cached}
    if pin is not None or consumer:
        result['paths'] = {name: str(store.object_path(item['sha256'])) for name, item in chosen.items()}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--budget', type=int)
    commands = parser.add_subparsers(dest='operation', required=True)
    for operation in ('inspect', 'acquire', 'run', 'archive'):
        p = commands.add_parser(operation)
        p.add_argument('reference', type=Path)
        p.add_argument('archive', type=Path, nargs='?', help='external local fixture; omit for v1 S3 source')
        p.add_argument('--max-expanded', type=int, default=DEFAULT_EXPANDED)
        p.add_argument('--manifest', type=Path, help='optional independently supplied expected member hashes')
        if operation in ('acquire', 'run'):
            selection = p.add_mutually_exclusive_group()
            selection.add_argument('--file', dest='selected', action='append')
            selection.add_argument('--full', action='store_true')
            if operation == 'acquire':
                p.add_argument('--pin')

        if operation == 'archive':
            p.add_argument('key', help='accepted unique store key to make archive-recoverable')
            p.add_argument('member', help='member name within reference subdirectory')
    argv = sys.argv[1:]
    command = []
    if '--' in argv:
        separator = argv.index('--')
        argv, command = argv[:separator], argv[separator + 1:]
    args = parser.parse_args(argv)
    if (args.operation == 'run') != bool(command):
        parser.error('run requires -- COMMAND; other operations do not take a command')
    store = None
    try:
        reference = reference_identity(read_json(args.reference, MAX_REFERENCE))
        expected = read_json(args.manifest, 8 * 1024 * 1024) if args.manifest else None
        if (expected is not None and manifest_members(expected) is not expected and
                'artifact' in expected and reference_identity(expected['artifact']) != reference):
            raise Refused('manifest artifact identity differs')
        if args.operation == 'inspect':
            result = inspect_archive(reference, args.archive, expected=expected,
                       max_expanded=args.max_expanded, budget=args.budget or 0)
        else:
            if args.root is None or args.budget is None:
                raise Refused('--root and --budget are required for managed acquisition')
            store = Store(args.root, args.budget)
            if args.operation == 'archive':
                name = member_name(args.member)
                limit = expansion_allowance(reference, args.max_expanded, budget=store.budget, expected=expected)
                members = scan_archive(args.archive, reference, expected=expected, max_expanded=limit, store=store)
                scope = scoped_members(reference, members)
                if name not in scope:
                    raise Refused('archived member missing')
                prefix = reference.get('subdirectory', '')
                full_name = prefix + '/' + name if prefix else name
                descriptor = recovery_descriptor(args.archive, reference, full_name, scope[name], limit, members)
                result = store.archive_recovery(args.key, descriptor, scope[name]['sha256'])
            else:
                result = acquire(store, reference, args.archive, selected=args.selected, full=args.full,
                                 pin=getattr(args, 'pin', None), max_expanded=args.max_expanded,
                                 expected=expected, consumer=args.operation == 'run')
                if args.operation == 'run':
                    result['consumer'] = store.run_consumer(result.pop('paths'), command)
        print(json.dumps(result, sort_keys=True))
        code = result.get('consumer', {}).get('returncode', 0)
        return code if code >= 0 else 128 - code
    except (Refused, OSError, ValueError, KeyError, TypeError, EOFError, tarfile.TarError, zlib.error) as error:
        print(json.dumps({'error': str(error)}), file=sys.stderr)
        return 2
    finally:
        if store is not None:
            store.close()


if __name__ == '__main__':
    sys.exit(main())
