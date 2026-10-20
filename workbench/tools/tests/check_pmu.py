#!/usr/bin/env python3
"""Check PMU collection preserves refused counters, raw events and original binary bytes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from pmu import probe


class PmuCollectionCheck(unittest.TestCase):
    def test_unavailable_is_not_successful_counting_and_root_errors_survive(self):
        with tempfile.TemporaryDirectory(prefix='pmu collection ') as temp:
            root = Path(temp)
            binary = root / 'original probe'
            binary.write_bytes(b'original executable bytes')
            output = root / 'collection'
            commands = []
            def execute(command, **kwargs):
                commands.append(command)
                if command[0] == 'sudo':
                    return subprocess.CompletedProcess(command, 1, '', 'sudo refused\n')
                mode = command[command.index('--pmu') + 1]
                receipt = {'measurement': {'status': 'disabled' if mode == 'off' else 'unavailable',
                                           'ipc': None, 'events': [{'name': 'cycles', 'count': None}]}}
                return subprocess.CompletedProcess(command, 0, json.dumps(receipt), '')
            argv = ['probe.py', str(binary), '--output', str(output), '--cpu', '7',
                    '--event', 'refill:4:0x2a', '--compare-root']
            with patch.object(sys, 'argv', argv), patch.object(probe.os, 'sched_getaffinity', return_value={7}), \
                    patch.object(probe, 'catalog', return_value={'cpu': 7}), \
                    patch.object(probe.subprocess, 'run', side_effect=execute):
                probe.main()
            meta = json.loads((output / 'probe.json').read_text())
            self.assertEqual(meta['binary_sha256'], hashlib.sha256(binary.read_bytes()).hexdigest())
            self.assertEqual((output / meta['binary']).read_bytes(), binary.read_bytes())
            self.assertEqual([p['status'] for p in meta['probes']],
                             ['disabled', 'unavailable', 'unavailable', 'unavailable'] + ['process_error'] * 4)
            self.assertEqual(commands[3][-2:], ['--pmu-event', 'refill:4:0x2a'])
            unavailable = json.loads((output / meta['probes'][1]['receipt']).read_text())
            self.assertIsNone(unavailable['measurement']['ipc'])
            self.assertIsNone(unavailable['measurement']['events'][0]['count'])
            self.assertEqual((output / meta['probes'][-1]['stderr']).read_text(), 'sudo refused\n')

    def test_disallowed_cpu_refuses_before_output_creation(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'unused'
            with patch.object(sys, 'argv', ['probe.py', 'missing', '--output', str(output), '--cpu', '8']), \
                    patch.object(probe.os, 'sched_getaffinity', return_value={7}), \
                    self.assertRaises(SystemExit):
                probe.main()
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
