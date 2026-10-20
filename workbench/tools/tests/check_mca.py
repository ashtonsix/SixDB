#!/usr/bin/env python3
"""Check real pinned cross-assembly/MCA regions, diagnostics and output isolation; no target execution."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'mca.py'


class McaCheck(unittest.TestCase):
    def setUp(self):
        for tool in ('llvm-mc-21', 'llvm-mca-21', 'llvm-objdump-21'):
            if not shutil.which(tool):
                self.skipTest('requires pinned LLVM tools')
        self.temp = tempfile.TemporaryDirectory(prefix='sixdb mca ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def invoke(self, source, triple, cpu, *extra, success=True):
        output = self.root / (source.stem + ' results')
        result = subprocess.run([sys.executable, TOOL, source, '--triple', triple,
            '--cpu', cpu, '--output', output, *extra], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return output, result

    def test_real_cross_regions_and_exact_command_scope(self):
        for name, triple, cpu, text in (
            ('avx2', 'x86_64-linux-gnu', 'znver5', 'vpcmpeqd %ymm0, %ymm1, %ymm2'),
            ('avx512', 'x86_64-linux-gnu', 'znver5', 'vpcmpeqd %zmm0, %zmm1, %k1'),
            ('neon', 'aarch64-linux-gnu', 'neoverse-v2', 'cmeq v2.4s, v0.4s, v1.4s'),
            ('m1', 'aarch64-linux-gnu', 'apple-m1', 'cmeq v2.4s, v0.4s, v1.4s')):
            with self.subTest(name=name):
                source = self.root / (name + ' source.s')
                body = '.text\n// LLVM-MCA-BEGIN chosen\n' + text + '\n// LLVM-MCA-END\n' + text + '\n'
                source.write_text(body)
                output, _ = self.invoke(source, triple, cpu, '--iterations=7', '--timeline')
                report = (output / 'mca.txt').read_text()
                self.assertRegex(report, r'Instructions:\s+7\b')
                self.assertIn('Code Region - chosen', report)
                self.assertEqual((output / 'input.s').read_text(), body)
                self.assertTrue((output / 'object.o').stat().st_size)
                self.assertIn('file format elf64', (output / 'disassembly.txt').read_text())
                commands = (output / 'commands.txt').read_text()
                self.assertIn('--noalias=false', commands)
                self.assertIn('--skip-unsupported-instructions=none', commands)
                self.assertIn('21.1.8', (output / 'versions.txt').read_text())
                self.invoke(source, triple, cpu, success=False)
                self.assertEqual((output / 'input.s').read_text(), body)

    def test_assembly_failure_and_ignored_features_remain_failures(self):
        for name, text, extra in (
            ('invalid', 'not_an_instruction', []),
            ('feature', 'addq %rdi, %rax', ['--mattr=+not-a-real-feature'])):
            with self.subTest(name=name):
                source = self.root / (name + '.s'); source.write_text(text + '\n')
                output, _ = self.invoke(source, 'x86_64-linux-gnu', 'znver5', *extra, success=False)
                self.assertTrue((output / 'assemble.stderr.txt').read_text())
                self.assertFalse((output / 'mca.txt').exists())

    def test_preprocessing_is_not_silently_ignored(self):
        source = self.root / 'conditional.S'
        source.write_text('#ifdef WRONG\naddq %rdi,%rax\n#endif\n')
        output, result = self.invoke(source, 'x86_64-linux-gnu', 'znver5', success=False)
        self.assertIn('Preprocess .S', result.stderr)
        self.assertFalse(output.exists())

    def test_opaque_calls_keep_their_warning(self):
        source = self.root / 'call.s'; source.write_text('cmpq $0, %rdi\nje .Ldone\ncall other_module\n.Ldone:\nretq\n')
        output, result = self.invoke(source, 'x86_64-linux-gnu', 'znver5', '--iterations=1', '--noalias')
        self.assertIn('call', (output / 'mca.stderr.txt').read_text().lower())
        self.assertIn('not cache/branch/callee/allocator', result.stdout)
        scope = (output / 'scope.txt').read_text()
        self.assertIn('2: call: callq', scope)
        self.assertIn('1: branch: je', scope)
        self.assertIn('3: return: retq', scope)
        self.assertIn('NOT complete module costs', scope)
        self.assertIn('--noalias=true', (output / 'commands.txt').read_text())
        self.assertIn('other_module', (output / 'disassembly.txt').read_text())


if __name__ == '__main__':
    unittest.main()
