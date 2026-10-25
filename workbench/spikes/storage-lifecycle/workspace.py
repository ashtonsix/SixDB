#!/usr/bin/env python3
"""Isolated spike: persistent capacity-limited command workspaces, not installed tooling.

Linux only; requires sudo -n, ext4/loop, mount/PID namespaces and Landlock ABI >= 3.
Each image is at most 128 MiB. No live builds or devices are formatted.
"""
import argparse
import ctypes
import errno
import fcntl
import json
import os
from pathlib import Path
import platform
import pwd
import re
import resource
import select
import selectors
import signal
import stat
import subprocess
import sys

MAX_IMAGE = 128 * 1024**2
MIN_IMAGE = 16 * 1024**2
MANAGER_RESERVE = 1024**2
MAX_NAMES = 128
MAX_REQUEST = 65536
LIBC = ctypes.CDLL(None, use_errno=True)


def checked(result):
    if result < 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number))
    return result


def syscall(number, *args):
    return checked(LIBC.syscall(ctypes.c_long(number), *args))


def regular(fd, owner=0):
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != owner:
        raise RuntimeError('expected a singly linked regular manager-owned file')
    return info


def directory(path, create=False):
    """Open every component without following symlinks; retain the final inode."""
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('pool must be an absolute path without ..')
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for index, part in enumerate(path.parts[1:]):
            if create and index == len(path.parts) - 2:
                try:
                    os.mkdir(part, 0o711, dir_fd=fd)
                    os.fsync(fd)
                except FileExistsError:
                    pass
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                              dir_fd=fd)
            os.close(fd)
            fd = next_fd
        info = os.fstat(fd)
        if info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o711:
            raise RuntimeError('pool must be root-owned mode 0711 (new pools are created this way)')
        return fd
    except BaseException:
        os.close(fd)
        raise


def open_at(pool, name, flags, mode=0o600):
    return os.open(name, flags | os.O_NOFOLLOW | os.O_CLOEXEC, mode, dir_fd=pool)


def tool(argv, descriptors=(), timeout=30):
    result = subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, pass_fds=descriptors, timeout=timeout,
                            env={'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LC_ALL': 'C'})
    if result.returncode:
        raise RuntimeError(f'{argv[0]} failed ({result.returncode}): ' +
                           result.stderr[-2000:].decode(errors='replace'))
    return result.stdout.decode().strip()


def procpath(fd, suffix=''):
    return f'/proc/self/fd/{fd}' + ('/' + suffix if suffix else '')


def loops(fd):
    return tool(['losetup', '--associated', procpath(fd)], (fd,))


def wrapper_alive(timeout=0):
    ready, _, _ = select.select([0], [], [], timeout)
    if ready and not os.read(0, 1):
        raise RuntimeError('wrapper lifeline closed')


def fresh_listdir(fd):
    # Btrfs records a directory-index ceiling when the open-file description is
    # created. A dirfd opened before flock can therefore miss a prior holder's
    # new entries even after the lock is acquired. dup()/listdir(fd) retains it.
    opened = os.open('.', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                     dir_fd=fd)
    try:
        return os.listdir(opened)
    finally:
        os.close(opened)


def validate_request(request):
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,47}', request['name']):
        raise ValueError('NAME must be 1-48 ASCII letters/digits/underscore/hyphen')
    size, budget = request['size'], request['budget']
    if (type(size) is not int or size < MIN_IMAGE or size > MAX_IMAGE or size % 4096 or
            type(budget) is not int or budget < size + MANAGER_RESERVE):
        raise ValueError('image must be 16-128 MiB, block aligned, and fit with 1 MiB manager reserve')
    if not request['command'] or not all(isinstance(s, str) for s in request['command']):
        raise ValueError('a command is required')


def ensure_budget(pool, budget):
    """Fixed tiny header shared by private workspaces and persistent volumes."""
    try:
        config = open_at(pool, 'budget', os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    except FileExistsError:
        config = open_at(pool, 'budget', os.O_RDONLY)
        try:
            regular(config)
            if os.read(config, 64) != (str(budget) + '\n').encode():
                raise RuntimeError('pool budget is immutable; supplied budget does not match')
        finally:
            os.close(config)
    else:
        try:
            os.write(config, (str(budget) + '\n').encode())
            os.fsync(config)
        finally:
            os.close(config)
        os.fsync(pool)


def prepare(pool, request, *, maximum_image=MAX_IMAGE, format_timeout=30):
    """Locked regular-image admission/allocation shared by both mount lifetimes.

    Private workspace CLI keeps its 128 MiB fixture cap. A persistent volume may
    supply another finite limit, while retaining the same maximum-capacity sum.
    """
    ensure_budget(pool, request['budget'])
    try:
        os.mkdir('mount', 0o711, dir_fd=pool)
        os.fsync(pool)
    except FileExistsError:
        pass
    mountfd = os.open('mount', os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=pool)
    try:
        info = os.fstat(mountfd)
        if info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o711 or fresh_listdir(mountfd):
            raise RuntimeError('unexpected mountpoint owner/mode/content')
    finally:
        os.close(mountfd)
    capacities, count = 0, 0
    for name in fresh_listdir(pool):
        if name in ('lock', 'budget', 'mount'):
            continue
        if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,47}\.(img|new)', name):
            raise RuntimeError('unknown pool entry; preserve and investigate: ' + name)
        fd = open_at(pool, name, os.O_RDWR)
        try:
            info = regular(fd)
            if stat.S_IMODE(info.st_mode) != 0o600:
                raise RuntimeError('unexpected image mode; preserve: ' + name)
            if loops(fd):
                raise RuntimeError('image is still associated with a loop device; refuse: ' + name)
            if name.endswith('.new'):
                # Never acknowledged: SIGKILL during allocation/format can leave this file.
                if info.st_size > maximum_image:
                    raise RuntimeError('oversized interrupted image; preserve: ' + name)
                os.unlink(name, dir_fd=pool)
                os.fsync(pool)
                continue
            if info.st_size < MIN_IMAGE or info.st_size > maximum_image or info.st_size % 4096:
                raise RuntimeError('invalid accepted image size; preserve: ' + name)
            capacities += info.st_size
            count += 1
        finally:
            os.close(fd)
    image = request['name'] + '.img'
    if image in fresh_listdir(pool):
        fd = open_at(pool, image, os.O_RDWR)
        if regular(fd).st_size != request['size']:
            os.close(fd)
            raise RuntimeError('named image capacity is immutable')
        if capacities + MANAGER_RESERVE > request['budget']:
            os.close(fd)
            raise RuntimeError('accepted images exceed configured budget; preserve and refuse')
        os.posix_fallocate(fd, 0, request['size'])
        os.fsync(fd)
        if regular(fd).st_blocks * 512 < request['size']:
            os.close(fd)
            raise RuntimeError('existing image allocation is incomplete; preserve and refuse')
        return fd, capacities
    if count >= MAX_NAMES or capacities + request['size'] + MANAGER_RESERVE > request['budget']:
        raise RuntimeError('capacity admission refused; accepted images are retained')
    staging = request['name'] + '.new'
    fd = open_at(pool, staging, os.O_RDWR | os.O_CREAT | os.O_EXCL)
    try:
        regular(fd)
        os.ftruncate(fd, request['size'])
        os.fsync(pool)  # Reserve maximum capacity before the first allocating write.
        os.posix_fallocate(fd, 0, request['size'])  # No sparse preallocation promise.
        if regular(fd).st_size != request['size']:
            raise RuntimeError('unexpected image size after allocation')
        os.fsync(fd)
        tool(['mkfs.ext4', '-F', '-q', '-m', '0', '-E',
              'nodiscard,lazy_itable_init=0,lazy_journal_init=0', procpath(fd)], (fd,),
             timeout=format_timeout)
        os.fsync(fd)
        if regular(fd).st_blocks * 512 < request['size']:
            raise RuntimeError('format discarded image allocation; refuse publication')
        os.rename(staging, image, src_dir_fd=pool, dst_dir_fd=pool)
        os.fsync(pool)
        return fd, capacities + request['size']
    except BaseException:
        os.close(fd)
        # Interrupted staging remains reserved until next locked reconciliation.
        raise


class MountAttributes(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in ('set', 'clear', 'propagation', 'userns')]


class PathRule(ctypes.Structure):
    _pack_ = 1
    _fields_ = [('access', ctypes.c_uint64), ('parent_fd', ctypes.c_int32)]


class Filter(ctypes.Structure):
    _fields_ = [('code', ctypes.c_uint16), ('yes', ctypes.c_uint8),
                ('no', ctypes.c_uint8), ('value', ctypes.c_uint32)]


class Program(ctypes.Structure):
    _fields_ = [('length', ctypes.c_uint16), ('filters', ctypes.POINTER(Filter))]


def constrain_descriptor_imports_and_mounts():
    """Retain stream networking; refuse mechanisms that import writable mounts.

    Classic seccomp cannot inspect recvmsg's ancillary-data pointer, so this
    deliberately refuses the whole recvmsg/recvmmsg family, not just SCM_RIGHTS.
    Creating a seccomp notification listener could delegate ADDFD to an outside
    helper; deny that syscall too after installing our own filter via prctl.
    """
    blocked = {
        212, 243, 417,  # recvmsg, recvmmsg, recvmmsg_time64
        438, 265,      # pidfd_getfd, open_by_handle_at
        425, 426, 427, # io_uring_setup/enter/register (includes async recvmsg)
        277,           # seccomp (notification listeners can inject descriptors)
        39, 40, 41, 97, 268,  # umount2, mount, pivot_root, unshare, setns
        428, 429, 430, 431, 432, 433, 442, 467,  # new mount API, incl open_tree_attr
    }
    # Enforced ARM64 syscall ABI; fail closed on an exec using another ABI.
    instructions = [(0x20, 0, 0, 4), (0x15, 1, 0, 0xc00000b7),
                    (0x06, 0, 0, 0x80000000), (0x20, 0, 0, 0)]
    for number in sorted(blocked):
        instructions += [(0x15, 0, 1, number), (0x06, 0, 0, 0x50000 | errno.EPERM)]
    instructions += [(0x06, 0, 0, 0x7fff0000)]
    filters = (Filter * len(instructions))(*(Filter(*item) for item in instructions))
    program = Program(len(filters), filters)
    checked(LIBC.prctl(22, 2, ctypes.byref(program), 0, 0))  # SECCOMP_MODE_FILTER


def readonly_except(workspace, shared_memory):
    # Clone first: loop backing-file and log descriptors retain the supervisor's
    # original writable mounts, rather than gaining a read-only backing file.
    checked(LIBC.unshare(0x00020000))  # CLONE_NEWNS
    readonly = MountAttributes(1, 0, 0, 0)  # MOUNT_ATTR_RDONLY
    try:
        syscall(442, -100, ctypes.c_char_p(b'/'), 0x8000, ctypes.byref(readonly), ctypes.sizeof(readonly))
    except OSError as exc:
        raise RuntimeError('recursive read-only host mounts: ' + str(exc)) from exc
    writable = MountAttributes(0, 1, 0, 0)
    try:
        syscall(442, -100, ctypes.c_char_p(os.fsencode(workspace)), 0x8000,
                ctypes.byref(writable), ctypes.sizeof(writable))
    except OSError as exc:
        raise RuntimeError('writable image mount: ' + str(exc)) from exc
    # Python sem_open/shm_open use /dev/shm. This is an alias of the capped
    # image, never a fresh tmpfs or access to the host's shared-memory directory.
    checked(LIBC.mount(ctypes.c_char_p(os.fsencode(shared_memory)),
                       ctypes.c_char_p(b'/dev/shm'), None, 4096, None))  # MS_BIND


def landlock(workspace):
    abi = syscall(444, 0, 0, 1)
    if abi < 3:
        raise RuntimeError('Landlock ABI >= 3 is required; no unsandboxed fallback')
    # All known allocating/mutating file rights. Read/execute remain available.
    handled = (1 << 1) | sum(1 << bit for bit in range(4, 15))
    if abi >= 5:
        handled |= 1 << 15  # Device ioctls outside workspace are denied too.
    ruleset = syscall(444, ctypes.byref(ctypes.c_uint64(handled)), 8, 0)
    try:
        for path, rights in ((workspace, handled), ('/dev/shm', handled), ('/dev/null', 1 << 1)):
            fd = os.open(path, os.O_PATH | os.O_CLOEXEC)
            try:
                rule = PathRule(rights, fd)
                syscall(445, ruleset, 1, ctypes.byref(rule), 0)
            finally:
                os.close(fd)
        checked(LIBC.prctl(38, 1, 0, 0, 0))  # PR_SET_NO_NEW_PRIVS
        syscall(446, ruleset, 0)
    finally:
        os.close(ruleset)
    return abi


def kill_and_reap():
    # This process is PID 1 in a new namespace, never the host namespace.
    if os.getpid() != 1:
        raise RuntimeError('refuse namespace-wide cleanup outside private PID namespace')
    try:
        os.kill(-1, signal.SIGKILL)
    except ProcessLookupError:
        pass
    while True:
        try:
            os.waitpid(-1, 0)
        except ChildProcessError:
            break


def supervise(pool, imagefd, request):
    target = procpath(pool, 'mount')
    mounted = False
    report = {'name': request['name'], 'capacity_bytes': request['size']}
    try:
        tool(['mount', '-t', 'ext4', '-o', 'loop,nodev,nosuid,noatime', procpath(imagefd), target],
             (pool, imagefd))
        mounted = True
        # Resolve only inside this root-owned, fd-anchored pool. The child's
        # source path remains visible, but the whole source mount becomes RO.
        workspace = target + '/work'
        try:
            os.mkdir(workspace, 0o700)
        except FileExistsError:
            if not stat.S_ISDIR(os.lstat(workspace).st_mode):
                raise RuntimeError('workspace is not a directory')
        uid, gid = int(os.environ['SUDO_UID']), int(os.environ['SUDO_GID'])
        os.chown(workspace, uid, gid)
        for name in ('home', 'tmp', 'cache', 'state', 'target', 'shm'):
            path = workspace + '/' + name
            # User contents are untrusted: do not follow symlinks as root.
            try:
                os.mkdir(path, 0o700)
                os.chown(path, uid, gid, follow_symlinks=False)
            except FileExistsError:
                if not stat.S_ISDIR(os.lstat(path).st_mode):
                    raise RuntimeError('reserved workspace path is not a directory: ' + name)
        # Logs live on the image, inaccessible by path to the unprivileged child.
        # Root directory is root-owned, so a full work/ cannot substitute them.
        outputs = []
        for name in ('stdout', 'stderr'):
            fd = os.open(target + '/' + name, os.O_CREAT | os.O_APPEND | os.O_RDWR |
                         os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
            regular(fd)
            outputs.append(fd)
        source = request['source']
        # Pool directories permit traversal, but root-only files cannot be read
        # or mutated. Ordinary toolchains need absolute cwd/temp/include paths.
        workfd = os.open(workspace, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        visible = os.path.realpath(workspace)
        env = request['env'] | {'HOME': visible + '/home', 'TMPDIR': visible + '/tmp',
                               'TMP': visible + '/tmp', 'TEMP': visible + '/tmp',
                               'XDG_CACHE_HOME': visible + '/cache', 'XDG_STATE_HOME': visible + '/state',
                               'CARGO_TARGET_DIR': visible + '/target',
                               'PYTHONPYCACHEPREFIX': visible + '/cache/pycache', 'PWD': visible,
                               'SIXDB_SOURCE': source, 'PYTHONDONTWRITEBYTECODE': '1'}

        def child_setup():
            try:
                # The fchdir holds the mounted work inode without exposing pool files.
                os.fchdir(workfd)
                os.close(workfd)
                readonly_except(str(Path(visible).parent), visible + '/shm')
                user = pwd.getpwuid(uid)
                os.setgroups(os.getgrouplist(user.pw_name, gid))
                os.setgid(gid)
                os.setuid(uid)
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                landlock('.')
                constrain_descriptor_imports_and_mounts()
                os.umask(0o077)
            except BaseException as exc:
                os.write(2, (repr(exc)[:2000] + '\n').encode())
                raise

        try:
            # No outside writable fd or lifeline is inherited. workfd is closed
            # by child_setup immediately after fchdir.
            try:
                consumer = subprocess.Popen(request['command'], stdin=subprocess.DEVNULL,
                                            stdout=outputs[0], stderr=outputs[1], env=env,
                                            preexec_fn=child_setup, pass_fds=(workfd,))
            except subprocess.SubprocessError as exc:
                raise RuntimeError('consumer sandbox setup failed: ' +
                                   os.pread(outputs[1], 2000, 0).decode(errors='replace')) from exc
            while consumer.poll() is None:
                ready, _, _ = select.select([0], [], [], 0.05)
                if ready and not os.read(0, 1):
                    report['wrapper_lost'] = True
                    break
            if report.get('wrapper_lost'):
                report['returncode'] = 125
            else:
                report['returncode'] = consumer.returncode
        finally:
            kill_and_reap()
            os.close(workfd)
            for label, fd in zip(('stdout_tail', 'stderr_tail'), outputs):
                size = os.fstat(fd).st_size
                report[label] = os.pread(fd, 1000, max(0, size - 1000)).decode(errors='replace')
            for fd in outputs:
                os.close(fd)
        fs = os.statvfs(target)
        report['available_bytes'] = fs.f_bavail * fs.f_frsize
        report['free_inodes'] = fs.f_ffree
    finally:
        if mounted:
            tool(['umount', target], (pool,))
            report['unmounted'] = True
    if loops(imagefd):
        raise RuntimeError('loop association remains after unmount; preserve and investigate')
    report['loop_released'] = True
    return report


def inside():
    if os.geteuid() != 0 or os.getpid() != 1 or not os.environ.get('SUDO_UID'):
        raise RuntimeError('internal mode requires sudo and a fresh private PID namespace')
    if platform.machine() != 'aarch64':
        raise RuntimeError('unreviewed syscall architecture')
    if Path('/proc/sys/kernel/core_pattern').read_text().startswith('|'):
        raise RuntimeError('external pipe core collector is outside this filesystem boundary')
    def interrupted(number, frame):
        raise RuntimeError('supervisor interrupted by signal ' + str(number))
    for number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(number, interrupted)
    # Do not buffer/read past the request: EOF is the wrapper lifetime signal.
    data = bytearray()
    while len(data) <= MAX_REQUEST:
        byte = os.read(0, 1)
        if byte == b'\n':
            break
        if not byte:
            raise RuntimeError('wrapper disappeared before request')
        data += byte
    else:
        raise ValueError('request too large')
    request = json.loads(data)
    validate_request(request)
    pool = directory(request['pool'], create=True)
    lock = open_at(pool, 'lock', os.O_CREAT | os.O_RDWR)
    lock_info = regular(lock)
    if lock_info.st_size or stat.S_IMODE(lock_info.st_mode) != 0o600:
        raise RuntimeError('unexpected lock content/mode; preserve and refuse')
    while True:
        wrapper_alive()
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            wrapper_alive(0.05)
    imagefd = None
    try:
        imagefd, capacities = prepare(pool, request)
        wrapper_alive()
        report = supervise(pool, imagefd, request)
        report.update({'reserved_image_bytes': capacities, 'manager_reserve_bytes': MANAGER_RESERVE,
                       'budget_bytes': request['budget'], 'image_bytes': regular(imagefd).st_size,
                       'image_allocated_bytes': regular(imagefd).st_blocks * 512})
        print(json.dumps(report, sort_keys=True), flush=True)
        return report['returncode'] if 0 <= report['returncode'] < 256 else 125
    finally:
        if imagefd is not None:
            os.close(imagefd)
        os.close(lock)
        os.close(pool)


def main():
    if sys.argv[1:] == ['--_inside']:
        return inside()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pool', required=True, type=Path)
    parser.add_argument('--budget', required=True, type=int)
    commands = parser.add_subparsers(dest='action', required=True)
    run = commands.add_parser('run')
    run.add_argument('name')
    run.add_argument('--size', required=True, type=int)
    arguments = sys.argv[1:]
    if '--' in arguments:
        separator = arguments.index('--')
        arguments, command = arguments[:separator], arguments[separator + 1:]
    else:
        command = []
    args = parser.parse_args(arguments)
    if not command:
        parser.error('separate the required command with --')
    request = {'pool': str(args.pool.absolute()), 'budget': args.budget, 'name': args.name,
               'size': args.size, 'command': command, 'source': os.getcwd(), 'env': dict(os.environ)}
    validate_request(request)
    encoded = (json.dumps(request) + '\n').encode()
    if len(encoded) > MAX_REQUEST:
        raise ValueError('command/environment too large')
    child = subprocess.Popen(['sudo', '-n', 'unshare', '--mount', '--pid', '--ipc',
                              '--mount-proc', '--propagation', 'private', '--fork', '--kill-child',
                              sys.executable, str(Path(__file__).resolve()), '--_inside'],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        child.stdin.write(encoded)
        child.stdin.flush()
        # Keep the lifeline's sole write descriptor open until the supervisor is
        # done. Command logs cannot fill these pipes: they are files on the image.
        buffers = {child.stdout: bytearray(), child.stderr: bytearray()}
        with selectors.DefaultSelector() as selector:
            for stream in buffers:
                selector.register(stream, selectors.EVENT_READ)
            while selector.get_map():
                for key, _ in selector.select():
                    block = os.read(key.fd, 4096)
                    if not block:
                        selector.unregister(key.fileobj)
                    else:
                        buffers[key.fileobj].extend(block)
                        if len(buffers[key.fileobj]) > MAX_REQUEST:
                            raise RuntimeError('unexpected supervisor output size')
        child.wait()
    finally:
        child.stdin.close()
    sys.stdout.buffer.write(buffers[child.stdout])
    sys.stderr.buffer.write(buffers[child.stderr])
    return child.returncode


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f'workspace refused: {type(exc).__name__}: {exc}', file=sys.stderr)
        raise SystemExit(125)
