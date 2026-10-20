#!/usr/bin/env python3
"""Build and run a native LMDB snapshot/abort/borrowed-byte example without a retired harness."""
from pathlib import Path
import subprocess
import tempfile
import unittest

PEERS = Path(__file__).resolve().parents[2] / 'peers'


class NativeLmdbCheck(unittest.TestCase):
    def test_imported_target_and_actual_transaction_consequences(self):
        with tempfile.TemporaryDirectory(prefix='native lmdb ') as temp:
            root = Path(temp)
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(native_peer LANGUAGES CXX)
include("${SIXDB_PEERS_ROOT}/lmdb.cmake")
include("${SIXDB_PEERS_ROOT}/lmdb.cmake")
add_executable(check "${SIXDB_PEERS_ROOT}/lmdb-check.cpp")
target_compile_features(check PRIVATE cxx_std_23)
target_link_libraries(check PRIVATE workbench::lmdb)
''')
            build = root / 'build'
            subprocess.run(['cmake', '-S', str(root), '-B', str(build), '-G', 'Ninja',
                            '-DCMAKE_CXX_COMPILER=clang++-21', '-DCMAKE_BUILD_TYPE=Release',
                            f'-DSIXDB_PEERS_ROOT={PEERS}'], check=True, capture_output=True)
            subprocess.run(['cmake', '--build', str(build), '-j1'], check=True, capture_output=True)
            result = subprocess.run([str(build / 'check'), str(root / 'database')],
                                    check=True, capture_output=True, text=True)
            self.assertIn('snapshot/abort/reserve/cursor checks passed', result.stdout)
            print(result.stdout.strip())
            graph = (build / 'build.ninja').read_text()
            self.assertNotIn('/workbench/benchmarks/orbital/', graph)
            self.assertNotIn('/workbench/spikes/orbital-mvcc/', graph)
            reused = subprocess.run([str(build / 'check'), str(root / 'database')], capture_output=True)
            self.assertNotEqual(reused.returncode, 0)


if __name__ == '__main__':
    unittest.main()
