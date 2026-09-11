#!/usr/bin/env python3
"""Exercise pinned archived executables, selective recovery and failure receipts offline."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import artifacts
import datasets
import replay


PROGRAM = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
int main(int argc, char** argv) {
    const char* output = nullptr;
    int repetitions = 0;
    bool sequential = false;
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--benchmark_out=", 16)) output = argv[i] + 16;
        if (!strncmp(argv[i], "--benchmark_repetitions=", 24)) repetitions = atoi(argv[i] + 24);
        if (!strcmp(argv[i], "--benchmark_enable_random_interleaving=false")) sequential = true;
    }
    if (!output || !repetitions || !sequential) return 9;
    const char* mode = getenv("REPLAY_CHECK_MODE");
    if (!mode) mode = "";
    if (!strcmp(mode, "exit")) return 7;
    if (!strcmp(mode, "nosamples")) return 0;
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed)) return 8;
    if (!getenv("SIXDB_CPU") || atoi(getenv("SIXDB_CPU")) != sched_getcpu()) return 11;
    const char* name = strstr(argv[0], "/candidate") ? "candidate_case" : "baseline_case";
    if (!strcmp(mode, "wrong")) name = "surprise_case";
    if (!strcmp(mode, "count")) --repetitions;
    FILE* file = fopen(output, "w");
    if (!file) return 10;
    fprintf(file, "{\"context\":{\"cpu\":%d,\"allowed_cpus\":%d},\"benchmarks\":[", sched_getcpu(), CPU_COUNT(&allowed));
    for (int i = 0; i < repetitions; ++i) {
        fprintf(file, "%s{\"run_type\":\"iteration\",\"run_name\":\"%s\",\"iterations\":%d,\"cpu_time\":1.0",
                i ? "," : "", name, !strcmp(mode, "zero") ? 0 : 1);
        if (!strcmp(mode, "error")) fprintf(file, ",\"error_occurred\":true,\"error_message\":\"fixture oracle failed\"");
        fprintf(file, "}");
    }
    fprintf(file, "]}");
    fclose(file);
}
'''


class ReplayCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compilation = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.compilation.cleanup)
        root = Path(cls.compilation.name)
        source = root / 'probe.cpp'
        source.write_text(PROGRAM)
        cls.binary = root / 'probe'
        subprocess.run(['clang++-21', '-std=c++23', '-O2', '-Werror', str(source), '-o', str(cls.binary)], check=True)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cache = self.root / 'cache'
        self.cpu = min(os.sched_getaffinity(0))
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, SIXDB_DATA_CACHE=str(self.cache), SIXDB_CPU=str(self.cpu),
                   REPLAY_CHECK_MODE='').start()
        self.spec_path = self.root / 'spec.json'
        self.output = self.root / 'output'
        self.objects = {}
        self.downloads = []
        self.spec = dict(format=1, variants={}, order=['baseline', 'candidate', 'candidate', 'baseline'],
                         filter='.*', min_time=0.001, repetitions=2, expected_cases=['baseline_case'])
        for label in ('baseline', 'candidate'):
            source = self.root / label
            (source / 'bin').mkdir(parents=True)
            binary = source / 'bin' / label
            shutil.copy2(self.binary, binary)
            digest = 'a' * 64
            (source / 'job.json').write_text(json.dumps({'source': {'digest': digest}}))
            (source / 'capture.json').write_text(json.dumps({'captured': label}))
            (source / 'unselected.asm').write_bytes(b'disassembly\n' * 100_000)
            bundle = self.root / (label + '.tar.gz')
            count = artifacts.pack(source, bundle, validate_run=False)
            identity = artifacts.sha256(bundle)
            reference = dict(format=1, kind='files', bucket='fixture', region='fixture', key=identity,
                             sha256=identity, bytes=bundle.stat().st_size, files=count)
            self.objects[identity] = bundle.read_bytes()
            variant = dict(artifact=reference, binary='bin/' + label, source_digest=digest,
                           files_sha256={'bin/' + label: datasets.digest(binary),
                                         'job.json': datasets.digest(source / 'job.json')})
            if label == 'candidate':
                variant.update(source_receipt='capture.json', source_digest=datasets.digest(source / 'capture.json'),
                               expected_cases=['candidate_case'])
                variant['files_sha256']['capture.json'] = datasets.digest(source / 'capture.json')
            self.spec['variants'][label] = variant
        patch.object(artifacts, 'aws', self.fake_aws).start()

    def fake_aws(self, *args, **kwargs):
        self.assertEqual(args[0], 'get-object')
        key = args[args.index('--key') + 1]
        self.downloads.append(key)
        if key not in self.objects:
            return subprocess.CompletedProcess(args, 1, '', 'fixture object missing')
        Path(args[-1]).write_bytes(self.objects[key])
        return subprocess.CompletedProcess(args, 0, '{}', '')

    def run_replay(self, output=None):
        self.spec_path.write_text(json.dumps(self.spec))
        return replay.replay(self.spec_path, output or self.output)

    def receipt(self):
        return json.loads((self.output / 'replay.json').read_text())

    def test_real_trials_pin_cpu_preserve_order_and_reuse_selected_cache(self):
        self.spec['environment'] = {'SIXDB_CPU': '-1'}  # Selected CPU also reaches self-pinning fixtures.
        self.run_replay()
        receipt = self.receipt()
        self.assertEqual(receipt['status'], 'complete')
        self.assertEqual(datasets.digest(self.output / 'inputs.json'), receipt['spec_sha256'])
        self.assertEqual([t['variant'] for t in receipt['trials']], self.spec['order'])
        self.assertEqual(len(self.downloads), 2)
        self.assertFalse(list(self.cache.rglob('unselected.asm')))
        self.assertFalse(list(self.output.rglob('unselected.asm')))
        for trial in receipt['trials']:
            data = json.loads((self.output / trial['samples']).read_text())
            self.assertEqual(data['context'], dict(cpu=self.cpu, allowed_cpus=1))
            self.assertGreater(trial['resources']['max_rss_kib'], 0)
            self.assertGreaterEqual(trial['resources']['wall_seconds'], 0)
            self.assertEqual(datasets.digest(self.output / trial['samples']), trial['samples_sha256'])
            self.assertEqual(trial['cases'], 1)
        self.run_replay(self.root / 'second')
        self.assertEqual(len(self.downloads), 2)
        with self.assertRaises(FileExistsError):
            self.run_replay()

    def test_cli_output_and_worker_defaults_with_existing_full_cache(self):
        # The original spike used a full-bundle cache; existing specs/caches still work.
        for label, variant in self.spec['variants'].items():
            target = self.cache / 'worker-bundles' / variant['artifact']['sha256']
            shutil.copytree(self.root / label, target)
        self.spec_path.write_text(json.dumps(self.spec))
        command = [sys.executable, str(Path(replay.__file__)), str(self.spec_path)]
        local = self.root / 'local'
        subprocess.run(command + ['--output', str(local), '--cpu', str(self.cpu)], check=True,
                       env=os.environ | {'SIXDB_CPU': '-1'}, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        worker = self.root / 'worker'
        subprocess.run(command, env=os.environ | {'SIXDB_RESULTS': str(worker)}, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(json.loads((local / 'replay.json').read_text())['status'], 'complete')
        self.assertEqual(json.loads((worker / 'replay/replay.json').read_text())['status'], 'complete')

    def test_corrupt_missing_and_unpinned_inputs_fail_before_execution(self):
        for mode in ('corrupt', 'missing', 'unpinned', 'source'):
            with self.subTest(mode=mode):
                original = copy.deepcopy(self.spec)
                variant = self.spec['variants']['baseline']
                key = variant['artifact']['key']
                saved = self.objects[key]
                if mode == 'corrupt':
                    self.objects[key] = b'corrupt'
                elif mode == 'missing':
                    del self.objects[key]
                elif mode == 'unpinned':
                    del variant['files_sha256']['bin/baseline']
                else:
                    variant['source_digest'] = 'b' * 64
                with self.assertRaises((RuntimeError, ValueError)):
                    self.run_replay()
                receipt = self.receipt()
                self.assertEqual(receipt['status'], 'failed')
                self.assertEqual(receipt['trials'], [])
                shutil.rmtree(self.output)
                self.objects[key] = saved
                self.spec = original

    def test_cached_bytes_are_checked_again(self):
        variant = self.spec['variants']['baseline']
        cached = self.cache / 'worker-bundles' / variant['artifact']['sha256']
        shutil.copytree(self.root / 'baseline', cached)
        for mode in ('changed', 'missing'):
            binary = cached / 'bin/baseline'
            if mode == 'changed':
                binary.write_bytes(b'changed')
            else:
                binary.unlink()
            with self.assertRaisesRegex(ValueError, 'archived input ' + mode):
                self.run_replay()
            self.assertEqual(self.receipt()['trials'], [])
            shutil.rmtree(self.output)

    def test_case_failures_keep_logs_resources_and_specific_errors(self):
        modes = dict(error='baseline_case: fixture oracle failed', wrong='missing=.*baseline_case.*surprise_case',
                     count='counts=.*baseline_case.*1', zero='invalid benchmark results',
                     exit='exited 7', nosamples='produced no samples file')
        for mode, message in modes.items():
            with self.subTest(mode=mode):
                self.spec['environment'] = {'REPLAY_CHECK_MODE': mode}
                with self.assertRaisesRegex(RuntimeError, message):
                    self.run_replay()
                receipt = self.receipt()
                self.assertEqual(receipt['status'], 'failed')
                self.assertEqual(len(receipt['trials']), 1)
                self.assertIn('resources', receipt['trials'][0])
                self.assertTrue((self.output / '01-baseline.txt').exists())
                shutil.rmtree(self.output)


if __name__ == '__main__':
    unittest.main()
