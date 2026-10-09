#!/usr/bin/env python3
"""Compile complete callers; model only exact straight-line found-row tails."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(argv):
    return subprocess.check_output(argv, text=True)


def inspect(obj, output, architecture):
    assembly = run(['llvm-objdump-21', '--no-show-raw-insn', '-drC', str(obj)])
    output.write_text(assembly)
    functions, current = {}, None
    for line in assembly.splitlines():
        header = re.match(r'^[0-9a-f]+ <(.*)>:$', line)
        if header:
            current = header[1]
            functions[current] = []
        elif current:
            instruction = re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9.]*)\s*(.*)', line)
            if instruction:
                functions[current].append(instruction[1] + ' ' + instruction[2])
            elif 'R_' in line and functions[current]:
                functions[current][-1] += ' [relocation: ' + line.strip() + ']'
    sizes = {}
    for line in run(['llvm-nm-21', '-S', '--demangle', '--radix=d', str(obj)]).splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) == 4 and fields[1].isdigit():
            sizes[fields[3]] = int(fields[1])
    result = []
    for name, body in functions.items():
        if not name.startswith(('critical_paths::', 'orbital_bench::lookup::')):
            continue
        frame = any(re.match(r'movq? %rsp, %rbp', line) for line in body)
        stack = [line for line in body if
                 (architecture == 'arm' and re.search(r'\b(sp|x29)\b', line)) or
                 (architecture == 'x86' and (re.search(r'%rsp\b', line) or
                    (frame and re.search(r'%rbp\b', line)) or re.match(r'^(push|pop)', line)))]
        calls = [line for line in body if re.match(r'^(bl|blr|callq?) ', line)]
        loads = [line for line in body if
                 (architecture == 'arm' and re.match(r'^ld', line)) or
                 (architecture == 'x86' and re.match(r'^mov|^v?pmov|^v?cvtsi', line)
                  and re.search(r'\([^)]*\),', line))]
        size = sizes.get(name)
        result.append({'name': name, 'bytes': size, 'instruction_sites': len(body),
                       'load_sites': len(loads), 'calls': calls, 'stack_sites': stack,
                       'cacheline64_span_min': (size + 63) // 64 if size else None,
                       'cacheline64_span_max': (size + 126) // 64 if size else None})
    return result


def compiler_functions(text, target):
    # Compiler assembly preserves exact instruction spelling/operands. These
    # leaves have identical Page/row/key/output signatures and zero branches.
    functions, current = {}, None
    for line in text.splitlines():
        header = re.match(r'^(_Z[^: ]+):', line)
        if header:
            current = header[1]
            functions[current] = []
        elif current:
            if re.match(r'^\.Lfunc_end', line):
                current = None
            elif line.lstrip().startswith(('.', '#', '//')) or not line.strip():
                continue
            else:
                instruction = line.strip().split('#' if target == 'zen5' else '//', 1)[0].strip()
                if instruction:
                    functions[current].append(instruction)
    return functions


def straight_line(body):
    branch = r'^(call\w*|bl|blr|br|b|b\.\w+|cbz|cbnz|tbz|tbnz|j\w+)\b'
    if any(re.match(branch, item) for item in body):
        return None
    returns = [i for i,item in enumerate(body) if re.match(r'^retq?\b',item)]
    if returns != [len(body)-1]:
        return None
    return body[:-1]


def parser_controls():
    symbol = '_Zpayload_fp_control'
    original = f'{symbol}:\n add x0, x0, #16 // keep the immediate\n ret\n.Lfunc_end0:\n'
    body = compiler_functions(original,'v2')[symbol]
    assert straight_line(body) == ['add x0, x0, #16']
    assert straight_line(['movq %rdi,%rax','retq']) == ['movq %rdi,%rax']
    refusals = ['b .Lx','b.eq .Lx','cbz x0, .Lx','cbnz x0, .Lx','tbz x0, #0, .Lx','tbnz x0, #0, .Lx','br x0','blr x0','bl callee','jmp .Lx','callq callee']
    for instruction in refusals:
        assert straight_line([instruction,'ret']) is None,instruction
    assert straight_line(['ret','add x0,x0,#16','ret']) is None
    return {'passed':True,'arm_immediate_preserved':True,'final_retq_accepted':True,'branch_call_refusals':len(refusals),'multiple_return_refused':True}


def model(assembly, output, target, cpu):
    text = assembly.read_text()
    functions = compiler_functions(text,target)
    lines = text.splitlines()
    results = []
    for symbol, body in functions.items():
        if not any(leaf in symbol for leaf in
                   ('payload_visible', 'payload_fused', 'payload_multiply', 'payload_simd', 'payload_fp')):
            continue
        # Refuse to turn calls or alternate paths into a fake whole-function model.
        original = body
        body = straight_line(body)
        if body is None:
            results.append({'symbol': symbol, 'status': 'not_straight_line', 'body': original})
            continue
        source = output / (target + '-' + symbol + '.s')
        constants = []
        for label in sorted(set(re.findall(r'\.LCPI\d+_\d+', '\n'.join(body)))):
            start = next(i for i,line in enumerate(lines) if line.startswith(label+':'))
            definition = [label+':']
            for line in lines[start+1:]:
                item = line.strip()
                if re.match(r'^\.(byte|short|long|word|quad|xword|zero|space|ascii|asciz)\b',item):
                    definition.append(line)
                elif item and not item.startswith(('#','//')):
                    break
            constants.extend(definition)
        source.write_text('# LLVM-MCA-BEGIN found-row-tail\n' + '\n'.join(body)
                          + '\n# LLVM-MCA-END\n' + ('.section .rodata\n'+'\n'.join(constants)+'\n' if constants else ''))
        object_file = source.with_suffix('.o')
        triple = 'x86_64-linux-gnu' if target == 'zen5' else 'aarch64-linux-gnu'
        assembler = ['clang-21','--target='+triple,
                     '-march=znver5' if target == 'zen5' else '-mcpu=neoverse-v2',
                     '-c',str(source),'-o',str(object_file)]
        subprocess.run(assembler,check=True)
        argv = ['llvm-mca-21', '-mtriple=' + ('x86_64' if target == 'zen5' else 'aarch64'),
                '-mcpu=' + cpu, '-iterations=100', '-all-stats', str(source)]
        execution = subprocess.run(argv, text=True, capture_output=True)
        source.with_suffix('.txt').write_text(execution.stdout)
        source.with_suffix('.stderr').write_text(execution.stderr)
        values = {}
        for line in execution.stdout.splitlines():
            match = re.match(r'^(Iterations|Instructions|Total Cycles|Total uOps|Dispatch Width|uOps Per Cycle|IPC|Block RThroughput):\s+(\S+)', line)
            if match:
                values[match[1]] = float(match[2])
        results.append({'symbol': symbol, 'argv': argv, 'status': execution.returncode,
                        'assembler_argv':assembler,'assembled_region_sha256':sha(object_file),
                        'constant_pool_definitions':constants,
                        'region_sha256': sha(source), 'summary': values,
                        'report': str(source.with_suffix('.txt'))})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse',type=Path,help='Reinspect hash-verified objects/assembly from a previous receipt; no C++ rebuild')
    args = parser.parse_args()
    root, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if args.reuse:
        previous = json.loads((args.reuse/'receipt.json').read_text())
        assert root == Path(previous['source_root']).resolve(), 'Reuse requires the original captured source root'
        capture = Path(str(root) + '.capture.json')
        assert sha(capture) == previous['capture_sha256'], 'Captured-source receipt changed'
        metadata = json.loads(capture.read_text())
        for name, digest in metadata['source_files_sha256'].items():
            assert sha(root/name) == digest, name
        previous['compiled_receipt_sha256'] = sha(args.reuse/'receipt.json')
        previous['analysis_recipe_sha256'] = sha(Path(__file__))
        previous['parser_controls'] = parser_controls()
        for target,entries in previous['targets'].items():
            folder = output/target;folder.mkdir()
            mca = folder/'mca';mca.mkdir()
            for entry in entries:
                obj=Path(entry['argv'][-1]);assembly=obj.with_suffix('.s')
                assert sha(obj)==entry['object_sha256'],obj
                assert sha(root/entry['source'])==entry['source_sha256'],entry['source']
                assert sha(assembly)==entry['compiler_assembly_sha256'],assembly
                entry['functions']=inspect(obj,folder/(obj.stem+'.asm'),'x86' if target=='zen5' else 'arm')
                if obj.stem in ('visible','fused'):
                    entry['mca']=model(assembly,mca,target,'znver5' if target=='zen5' else 'neoverse-v2')
        (output/'receipt.json').write_text(json.dumps(previous,indent=2)+'\n')
        print('Reinspected verified objects; corrected/assembled exact regions; no C++ recompilation')
        return
    study = Path('workbench/spikes/call-boundaries/critical-paths')
    sources = [study / p for p in ('seams.cpp', 'ordinary.cpp', 'visible.cpp', 'fused.cpp')]
    sources += [Path('workbench/benchmarks/orbital/lookup.cpp')]
    common = ['-std=c++23', '-O3', '-g0', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
              '-ffp-contract=off', '-fno-fast-math', '-fvisibility=hidden',
              '-I' + str(root / 'loom/include'), '-I' + str(root / 'orbital/include'),
              '-I' + str(root / 'ikea/include')]
    cross = ['--target=x86_64-linux-gnu', '--sysroot=/usr/x86_64-linux-gnu',
             '-isystem', '/usr/x86_64-linux-gnu/include/c++/15',
             '-isystem', '/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu']
    targets = {'zen5': ('x86', cross + ['-march=znver5'], 'znver5'),
               'v2': ('arm', ['-mcpu=neoverse-v2'], 'neoverse-v2')}
    capture = Path(str(root) + '.capture.json')
    receipt = {'compiler': run(['clang++-21', '--version']),
               'mca': run(['llvm-mca-21', '--version']).splitlines()[0],
               'recipe_sha256': sha(Path(__file__)), 'source_root': str(root),
               'capture_sha256': sha(capture),
               'source_digest': json.loads(capture.read_text())['source_digest'],
               'limits': ['Static sites and compiler CPU model, not dynamic stalls or hardware throughput.',
                          'MCA covers exactly the successful found-row tail; excludes directory/slot search, admission, ownership setup and branch behavior.',
                          'MCA load latencies and alias/resource assumptions are its pinned CPU model; no physical-address/cache simulation.',
                          'No ordinary call is modeled as a cheap inlined callee. Complete functions and relocation-resolved calls remain separately inspected.',
                          '64-byte code spans are min/max alignment bounds, not observed I-cache misses.',
                          'Zen5 compile-only; different native and cross standard-library header versions remain recorded in arguments.'],
               'parser_controls':parser_controls(),'targets': {}}
    for target, (arch, flags, cpu) in targets.items():
        directory = output / target
        directory.mkdir()
        mca = directory / 'mca'; mca.mkdir()
        entries = []
        for source in sources:
            obj = directory / (source.stem + '.o')
            argv = ['clang++-21', *common, *flags, '-c', str(root/source), '-o', str(obj)]
            subprocess.run(argv, check=True)
            assembly = obj.with_suffix('.s')
            subprocess.run(['clang++-21', *common, *flags, '-S', str(root/source), '-o', str(assembly)], check=True)
            entries.append({'source': str(source), 'source_sha256': sha(root/source),
                            'object_sha256': sha(obj), 'argv': argv,
                            'compiler_assembly_sha256':sha(assembly),
                            'functions': inspect(obj, obj.with_suffix('.asm'), arch),
                            'mca': model(assembly, mca, target, cpu) if source.stem in ('visible','fused') else []})
        receipt['targets'][target] = entries
        (output/'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(target + ': compiled and inspected complete callers; modeled exact found-row tails', flush=True)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
