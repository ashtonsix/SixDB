#!/usr/bin/env python3
"""Exercise real APT retries against a local repository and preserve compiler-pin failure."""
from collections import Counter
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'worker-setup.sh'


class AptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        package = self.root / 'package'
        (package / 'DEBIAN').mkdir(parents=True)
        (package / 'DEBIAN/control').write_text('Package: fixture-worker-setup\nVersion: 1\nArchitecture: all\n'
            'Maintainer: Fixture <fixture@example.invalid>\nDescription: disposable retry fixture\n')
        archive = self.root / 'fixture.deb'
        subprocess.run(['dpkg-deb', '--build', '--root-owner-group', str(package), str(archive)],
                       check=True, capture_output=True)
        data = archive.read_bytes()
        packages = ('Package: fixture-worker-setup\nVersion: 1\nArchitecture: all\nFilename: fixture.deb\n'
                    f'Size: {len(data)}\nSHA256: {hashlib.sha256(data).hexdigest()}\n'
                    'Description: disposable retry fixture\n\n').encode()
        release = ('Suite: fixture\nCodename: fixture\nSHA256:\n'
                   f' {hashlib.sha256(packages).hexdigest()} {len(packages)} Packages\n').encode()
        files = {'/Release': release, '/Packages': packages, '/fixture.deb': data}
        self.requests, self.failures = Counter(), Counter()
        owner = self
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                path = self.path.replace('/./', '/')
                owner.requests[path] += 1
                if owner.failures[path]:
                    owner.failures[path] -= 1
                    code, body = 503, b'transient fixture outage'
                else:
                    code, body = (200, files[path]) if path in files else (404, b'absent')
                self.send_response(code)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *args):
                pass
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        thread.start()
        def stop():
            self.server.shutdown()
            self.server.server_close()
            thread.join()
        self.addCleanup(stop)
        for directory in ('empty', 'state/lists/partial', 'cache/archives/partial', 'log', 'downloads'):
            (self.root / directory).mkdir(parents=True)
        (self.root / 'state/status').touch()
        # Trust only this unsigned local fixture. The production script's signed-by is unchanged.
        (self.root / 'sources.list').write_text(f'deb [trusted=yes] http://127.0.0.1:{self.server.server_port} ./\n')
        settings = {'Dir::Etc::parts': 'empty', 'Dir::Etc::main': 'empty/main',
                    'Dir::Etc::sourcelist': 'sources.list', 'Dir::Etc::sourceparts': 'empty',
                    'Dir::State': 'state', 'Dir::State::status': 'state/status',
                    'Dir::Cache': 'cache', 'Dir::Log': 'log'}
        self.apt_config = self.root / 'apt.conf'
        self.apt_config.write_text(''.join(f'{key} "{self.root / value}";\n' for key, value in settings.items()) +
            'Acquire::Languages "none";\nAcquire::http::Proxy::127.0.0.1 "DIRECT";\n')

    def apt(self, *args):
        definitions = SCRIPT.read_text().split('\napt_get update', 1)[0]
        return subprocess.run(['bash', '-c', definitions + '\napt_get "$@"', 'fixture', *args],
            env=os.environ | {'APT_CONFIG': str(self.apt_config)}, cwd=self.root / 'downloads',
            text=True, capture_output=True, timeout=30)

    def test_metadata_and_package_fetch_recover_after_transient_errors(self):
        self.failures['/InRelease'] = 2
        result = self.apt('update', '-qq')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests['/InRelease'], 3)
        self.failures['/fixture.deb'] = 2
        result = self.apt('download', 'fixture-worker-setup')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests['/fixture.deb'], 3)
        self.assertEqual(next((self.root / 'downloads').glob('*.deb')).read_bytes(),
                         (self.root / 'fixture.deb').read_bytes())

    def test_exhausted_metadata_fetch_fails_even_with_an_existing_index(self):
        result = self.apt('update', '-qq')
        self.assertEqual(result.returncode, 0, result.stderr)
        before = self.requests['/InRelease']
        self.failures['/InRelease'] = 100
        result = self.apt('update', '-qq')
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn('Failed to fetch', result.stderr)
        self.assertEqual(self.requests['/InRelease'] - before, 4)


class CompilerPinTests(unittest.TestCase):
    def test_setup_keeps_the_exact_compiler_check_and_stops_on_install_failure(self):
        for version, apt_status, expected in [('21.1.8', 0, 0), ('21.1.7', 0, 1), ('21.1.80', 0, 1), ('21.1.8', 100, 100)]:
            with self.subTest(version=version, apt_status=apt_status), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                binary = root / 'bin'
                binary.mkdir()
                mock = binary / 'command'
                mock.write_text('''#!/usr/bin/python3
import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
with open(os.environ['SETUP_TEST_LOG'], 'a') as out:
    out.write(json.dumps([name, *sys.argv[1:]]) + '\\n')
if name == 'clang++-21': print('Ubuntu clang version ' + os.environ['SETUP_TEST_VERSION'])
if name == 'apt-get': sys.exit(int(os.environ['SETUP_TEST_APT_STATUS']))
''')
                mock.chmod(0o755)
                for name in ('apt-get', 'clang++-21', 'curl', 'gpg', 'systemctl'):
                    (binary / name).symlink_to(mock)
                script = SCRIPT.read_text().replace('/etc/apt/sources.list.d/sixdb-llvm.list', str(root / 'llvm.list'))
                log = root / 'commands.jsonl'
                result = subprocess.run(['bash', '-c', script], text=True, capture_output=True,
                    env=os.environ | {'PATH': str(binary) + ':' + os.environ['PATH'], 'SETUP_TEST_LOG': str(log),
                        'SETUP_TEST_VERSION': version, 'SETUP_TEST_APT_STATUS': str(apt_status)})
                self.assertEqual(result.returncode, expected, result.stderr)
                commands = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual(any(c[0] == 'systemctl' for c in commands), expected == 0)
                if apt_status:
                    self.assertFalse(any(c[0] == 'clang++-21' for c in commands))
                elif version != '21.1.8':
                    self.assertTrue(any(c[0] == 'apt-get' and 'clang-21' in c for c in commands))


if __name__ == '__main__':
    unittest.main()
