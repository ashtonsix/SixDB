#!/usr/bin/env python3
"""Assemble a literal .s and retain a pinned llvm-mca report, commands and diagnostics."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='literal/preprocessed assembler; LLVM-MCA regions are preserved')
    parser.add_argument('--triple', required=True)
    parser.add_argument('--cpu', required=True, help='explicit model selector, never inferred from this host')
    parser.add_argument('--mattr', default='', help='LLVM feature list; use --mattr=+avx2,-avx512f spelling')
    parser.add_argument('--iterations', type=int, default=100)
    parser.add_argument('--noalias', action='store_true', help='explicitly assume loads/stores do not alias; default is false')
    parser.add_argument('--timeline', action='store_true')
    parser.add_argument('--output', type=Path, required=True, help='new output directory')
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error('--iterations must be positive')
    try:
        source = args.source.resolve().read_bytes()
        if re.search(rb'^\s*#\s*(?:if|ifdef|ifndef|elif|else|endif|define|undef|include)\b', source, re.M):
            raise ValueError('Preprocess .S with its configured compile_probe.py --assembly command first')
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        (output / 'input.s').write_bytes(source)
        pin = re.search(r'set\(SIXDB_CLANG_VERSION "([^"]+)"\)',
                        (ROOT / 'cmake/ClangVersion.cmake').read_text())[1]
        major = pin.split('.')[0]
        programs = {}
        with (output / 'commands.txt').open('w') as commands, (output / 'versions.txt').open('w') as versions:
            commands.write(f'# input {args.source.resolve()}\n# sha256 {hashlib.sha256(source).hexdigest()}\n')
            for name in ('llvm-mc', 'llvm-mca', 'llvm-objdump'):
                path = shutil.which(f'{name}-{major}')
                if not path:
                    raise ValueError(f'Missing {name}-{major}')
                argv = [path, '--version']
                commands.write(shlex.join(argv) + '\n'); commands.flush()
                version = subprocess.run(argv, capture_output=True, text=True, check=True).stdout
                versions.write(f'$ {shlex.join(argv)}\n{version}\n'); versions.flush()
                if not re.search(r'\bversion\s+' + re.escape(pin) + r'\b', version):
                    raise ValueError(f'{name} does not match pinned LLVM {pin}; see versions.txt')
                programs[name] = path

            def run(argv, name, extension='txt'):
                commands.write(shlex.join(argv) + '\n'); commands.flush()
                with (output / (name + '.' + extension)).open('w') as stdout, \
                        (output / (name + '.stderr.txt')).open('w') as stderr:
                    result = subprocess.run(argv, stdout=stdout, stderr=stderr)
                diagnostics = (output / (name + '.stderr.txt')).read_text()
                if result.returncode or 'not a recognized' in diagnostics:
                    raise ValueError(f'{name} failed or ignored a requested target; see {name}.stderr.txt')
                return diagnostics

            target = ['-mcpu=' + args.cpu, '-mattr=' + args.mattr]
            run([programs['llvm-mc'], '-triple=' + args.triple, *target,
                 '-filetype=obj', str(output / 'input.s'), '-o', str(output / 'object.o')], 'assemble')
            run([programs['llvm-objdump'], '-dr', str(output / 'object.o')], 'disassembly')
            argv = [programs['llvm-mca'], '-mtriple=' + args.triple, *target,
                    '--iterations=' + str(args.iterations),
                    '--noalias=' + str(args.noalias).lower(), '--skip-unsupported-instructions=none',
                    '--bottleneck-analysis', '--all-stats']
            if args.timeline:
                argv += ['--timeline', '--timeline-max-iterations=3', '--timeline-max-cycles=80']
            run([*argv, str(output / 'input.s')], 'mca')
            # Native LLVM JSON identifies the exact selected region instructions;
            # don't guess regions or control flow from C++/assembler source text.
            run([*argv, '--json', str(output / 'input.s')], 'mca-structured', 'json')
            report = json.loads((output / 'mca-structured.json').read_text())
            with (output / 'scope.txt').open('w') as scope:
                scope.write('Literal backend sequence estimates. No branch probabilities/PC traversal, cache hierarchy, store forwarding or callee execution.\n')
                scope.write('Recognized x86/AArch64 control-flow sites follow. Branches can be tail calls; inspect linked targets.\n')
                scope.write('Calls retain LLVM placeholders, NOT complete module costs. Unsupported instructions are never skipped.\n')
                scope.write(f'CPU selector: {args.cpu}; noalias={args.noalias}\n')
                resources = report.get('TargetInfo', {}).get('Resources', [])
                scope.write('Reported resources: ' + json.dumps(resources) + '\n')
                for region in report['CodeRegions']:
                    scope.write('\nRegion: ' + region.get('Name', '') + '\n')
                    summary = region['SummaryView']
                    scope.write(f'Model block reciprocal throughput: {summary["BlockRThroughput"]} cycles; '
                                f'total {summary["TotalCycles"]} synthetic cycles / {summary["Iterations"]} repetitions\n')
                    sites = []
                    for index, instruction in enumerate(region['Instructions']):
                        words = instruction.lower().split()
                        while words and words[0] in {'notrack', 'bnd', 'rep', 'repz', 'repnz'}:
                            words.pop(0)
                        mnemonic = words[0] if words else ''
                        kind = ('call' if mnemonic.startswith('call') or mnemonic in {'bl', 'blr', 'blraa', 'blrab', 'blraaz', 'blrabz'} else
                                'return' if mnemonic.startswith('ret') or mnemonic == 'eret' else
                                'branch' if mnemonic.startswith(('j', 'loop', 'b.')) or mnemonic in {'b', 'br', 'braa', 'brab', 'cbz', 'cbnz', 'tbz', 'tbnz'} else '')
                        if kind:
                            sites.append(f'{index}: {kind}: {instruction}')
                    scope.write('\n'.join(sites) + '\n' if sites else 'No recognized call/return/branch in selected region.\n')
                    unmodeled = [v['Instruction'] for v in region['InstructionInfoView']['InstructionList']
                                 if v.get('hasUnmodeledSideEffects')]
                    scope.write(f'LLVM hasUnmodeledSideEffects instruction indices: {unmodeled}\n')
        print(f'Model report: {output / "mca.txt"} (scope/control flow: scope.txt; warnings: mca.stderr.txt)')
        print('Backend scheduling estimate only; not cache/branch/callee/allocator or full-operation time.')
        print('CPU/feature flags do not certify the instruction set of hand-written x86 assembly.')
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
