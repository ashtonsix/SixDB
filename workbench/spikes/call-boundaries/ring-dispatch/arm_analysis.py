#!/usr/bin/env python3
"""Independent offline qualification of the frozen ARM serial-gap diagnostic.

No downloaded code is executed. Checksum/request counts are reconstructed from
the documented key-set law, independently of table kernels or shuffle order.
The CSV does not expose terminal keys or individual found values: those remain
the captured executable's internal answer checks, not offline observations.
"""
import argparse
import csv
import datetime
import functools
import gzip
import hashlib
import json
import math
from pathlib import Path
import shlex
import re
import statistics
import struct
import tarfile
import tempfile

ARMS = ('abseil', 'abseil_prefetch', 'fused', 'manual', 'manual_hints', 'ring', 'ring_hints')
CASES = (('hot_serial', 8, 3276, 1), ('hot_parallel', 8, 3276, 32),
         ('large_aged_serial', 20, 13421772, 1), ('large_aged_parallel', 20, 13421772, 32))
MASK64 = (1 << 64) - 1
MASK40 = (1 << 40) - 1
ABSENT = 1 << 39
PHYSICAL = ('requests', 'checksum', 'payload_bytes', 'backing_bytes', 'known_metadata_bytes',
            'shared_preparation_bytes', 'abseil_payload_bytes', 'untimed_key_input_bytes',
            'primary_entries', 'secondary_entries', 'chain_entries', 'primary_visits',
            'secondary_visits', 'chain_directory_visits', 'chain_body_visits',
            'chain_payload_visits', 'staged_hint_line_targets', 'primary_buckets',
            'primary_stride', 'secondary_buckets', 'secondary_stride')
INITIAL_JOB = '20261013T110137Z-1e139b98'
INITIAL_DIGEST = '677ff07f6d6913373ab13c384a5038d43b0a4bf6031ba80c4772a3afe1ad6184'
INITIAL_MANIFEST_SHA = 'ff600878e59ef54925fb9736f98c6fa0f4ef3b10c45b8151305e263d80fce170'
INITIAL_SOURCE_SHA = '7810f7409a11f2e1c14ed9d6466913146aba42fc5341db69f053660d6c113015'


def check(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text())


def mix64(x):
    x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & MASK64
    return x ^ (x >> 31)


def permute40(x):
    # Forty-bit permutation seeded by the workload seed, not the map hash seed.
    x = (x ^ 87191) & MASK40
    x = ((x ^ (x >> 18)) * 0xbf58476d1ce4e5b9) & MASK40
    x = ((x ^ (x >> 16)) * 0x94d049bb133111eb) & MASK40
    return x ^ (x >> 19)


@functools.lru_cache(None)
def expected_work(n, generation=8, mixed=True):
    total, misses, cycle_steps = 0, 0, 0
    for i in range(generation * n, (generation + 1) * n):
        key = permute40(i)
        cycle_steps += 1
        while key & ABSENT:
            key = permute40(key)
            cycle_steps += 1
        absent = bool(mixed and (mix64(key) & 1))
        total += key | (ABSENT if absent else 0)
        misses += absent
    # Shuffling only reorders the complete cycle. Each present key returns its
    # successor request exactly once; absent mates return zero before retrying.
    return {'checksum': total & MASK64, 'requests': n + misses,
            'present_results': n, 'absent_results': misses,
            'cycle_walk_steps': cycle_steps, 'origin': 'independent_key_set_law'}


def context(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            check(key not in result, f'duplicate context {key}: {path}')
            result[key] = value
    return result


def check_cell(csv_path, context_path, case, reps, reference=None, generation=8, mixed=True, frame_reference=None):
    name, a, n, concurrency = case
    rows = list(csv.DictReader(Path(csv_path).open()))
    expected_keys = [(rep, ARMS[(order + rep) % 7]) for rep in range(reps) for order in range(7)]
    check([(int(r['repetition']), r['execution']) for r in rows] == expected_keys,
          f'cyclic arm order/repetition count: {csv_path}')
    ctx = context(context_path)
    for key in ('hardware_context', 'calibration_enabled', 'policy_revision', 'line_bytes', 'page_bytes', 'hint_code'):
        check(key in ctx, f'missing policy context {key}: {context_path}')
    check(ctx['calibration_enabled'] == 'false', f'background calibration: {context_path}')
    line, page, hint = (int(ctx[key]) for key in ('line_bytes', 'page_bytes', 'hint_code'))
    check(int(ctx['policy_revision']) >= 0 and line > 0 and line & (line - 1) == 0
          and page >= line and page & (page - 1) == 0 and hint in (0, 1, 2, 3),
          f'invalid hint policy/geometry: {context_path}')
    work = expected_work(n, generation, bool(mixed))
    rounds = max(1, (131072 + work['requests'] - 1) // work['requests'])
    invariant = {field: int(rows[0][field]) for field in PHYSICAL}
    frames = {}
    for arm in ARMS:
        shapes = {(int(row['job_state_bytes']), int(row['ring_storage_bytes']))
                  for row in rows if row['execution'] == arm}
        check(len(shapes) == 1, f'frame dimensions changed within arm: {csv_path}/{arm}')
        job_bytes, ring_bytes = next(iter(shapes))
        check(job_bytes % concurrency == 0 and job_bytes >= concurrency * 32, f'caller frame dimensions: {csv_path}')
        check((ring_bytes > 0) == (arm in ('ring', 'ring_hints')), f'Ring storage presence: {csv_path}')
        frames[arm] = {'job_bytes': job_bytes, 'job_bytes_per_stream': job_bytes // concurrency, 'ring_bytes': ring_bytes}
    check(all(frames[arm]['job_bytes_per_stream'] == 32 for arm in ARMS[:3]), f'ordinary lane dimensions: {csv_path}')
    check(len({frames[arm]['job_bytes_per_stream'] for arm in ARMS[3:]}) == 1
          and frames['ring']['ring_bytes'] == frames['ring_hints']['ring_bytes'], f'matched caller frame dimensions: {csv_path}')
    if frame_reference is not None:
        check(frames == frame_reference, f'frame dimensions changed across factors/passes: {csv_path}')
    for row in rows:
        check((int(row['entries']), int(row['concurrency']), int(row['turnovers']), int(row['seed'])) ==
              (n, concurrency, generation, 37913), f'case qualifiers: {csv_path}')
        check(row['queries'] == ('mixed' if mixed else 'hit') and int(row['rounds']) == rounds, f'work/rounds: {csv_path}')
        check(int(row['checksum']) == work['checksum'] and int(row['requests']) == work['requests'],
              f'independent checksum/request count: {csv_path}')
        check(all(int(row[f]) == invariant[f] for f in PHYSICAL), f'physical work changes by arm: {csv_path}')
        value = float(row['ns_per_lookup'])
        check(math.isfinite(value) and value > 0, f'invalid timing: {csv_path}')
        check(int(row['hint_code']) == int(ctx['hint_code']), f'hint selection mismatch: {csv_path}')
        check(int(row['primary_entries']) + int(row['secondary_entries']) + int(row['chain_entries']) == n,
              f'population: {csv_path}')
        check(int(row['primary_visits']) == work['requests'], f'primary traversal count: {csv_path}')
        check(int(row['chain_directory_visits']) == int(row['chain_body_visits']) == int(row['chain_payload_visits']),
              f'chain stages: {csv_path}')
        check(int(row['untimed_key_input_bytes']) == n * 8 and int(row['backing_bytes']) >= int(row['payload_bytes']),
              f'physical allocation accounting: {csv_path}')
        check(int(row['staged_hint_line_targets']) >= work['requests'], f'hint target count: {csv_path}')
        check(int(row['primary_buckets']) == 1 << a and int(row['secondary_buckets']) == 1 << (a - 3),
              f'layout dimensions: {csv_path}')
        if not mixed:
            check(int(row['secondary_visits']) == int(row['secondary_entries']) + int(row['chain_entries']),
                  f'complete hit-cycle secondary traversal: {csv_path}')
            if int(row['chain_entries']) == 0:
                check(int(row['chain_directory_visits']) == 0, f'chain traversal with no chained keys: {csv_path}')
    if reference is not None:
        check(invariant == reference['physical'], f'captured physical/stage/hint baseline changed: {csv_path}')
        check(all(ctx[key] == reference['context'][key]
                  for key in ('policy_revision', 'line_bytes', 'page_bytes', 'hint_code')),
              f'captured policy/geometry changed: {context_path}')
    medians = {arm: statistics.median(float(r['ns_per_lookup']) for r in rows if r['execution'] == arm) for arm in ARMS}
    samples = {arm: [float(r['ns_per_lookup']) for r in rows if r['execution'] == arm] for arm in ARMS}
    return {'case': name, 'concurrency': concurrency, 'entries': n, 'rows': len(rows), 'repetitions': reps,
            'turnovers': generation, 'mixed': bool(mixed), 'frame_dimensions': frames,
            'csv_sha256': sha(csv_path), 'context_sha256': sha(context_path), 'context': ctx,
            'physical': invariant, 'independent_work': work, 'medians_ns': medians, 'samples_ns': samples,
            'own_manual_hints_gap_ns': medians['ring_hints'] - medians['manual_hints'],
            'own_manual_hints_ratio': medians['ring_hints'] / medians['manual_hints'],
            'terminal_check': 'not_exposed_in_csv; captured executable checks complete answer including terminal',
            'stage_hint_population_origin': 'logical_traversal_and_allocation_counters; cross-factor and original-capture equality'}


def check_frozen_source(launch):
    job = load(launch / 'job.json')
    manifest = load(launch / 'source-manifest.json')
    if job['id'] == INITIAL_JOB:
        check(job['source']['digest'] == INITIAL_DIGEST, 'initial diagnostic source changed')
        check(sha(launch / 'source-manifest.json') == INITIAL_MANIFEST_SHA, 'initial frozen manifest changed')
        check(sha(launch / 'source.tar.gz') == INITIAL_SOURCE_SHA, 'initial frozen source archive changed')
    check(sha(launch / 'source.tar.gz') == job['source']['sha256'], 'frozen source archive differs from launch')
    check(hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest() == job['source']['digest'],
          'worker content digest')
    with tarfile.open(launch / 'source.tar.gz') as archive:
        members = archive.getmembers()
        check(all(m.isfile() for m in members), 'non-file source archive members')
        check({m.name for m in members} == set(manifest) and len(members) == len(manifest), 'source archive inventory')
        for member in members:
            check(hashlib.sha256(archive.extractfile(member).read()).hexdigest() == manifest[member.name],
                  f'captured source file hash {member.name}')
        provenance = json.load(archive.extractfile('controls/provenance.json'))
    return job, manifest, provenance


def check_source(results, launch):
    job, manifest, provenance = check_frozen_source(launch)
    manifest_sha = sha(launch / 'source-manifest.json')
    archive_sha = sha(launch / 'source.tar.gz')
    observed_job = load(results / 'job.json')
    assignment = load(launch / 'assignment.json')
    assigned_id = assignment['worker_id']
    check(Path(assigned_id).name == assigned_id and assigned_id not in ('.', '..'), 'unsafe worker assignment ID')
    launch_receipt = launch / 'launch.json'
    if not launch_receipt.exists():
        launch_receipt = launch.parent / assigned_id / 'launch.json'
    launched = load(launch_receipt)
    # The runtime adds its assignment and actual capacity after launch. Retain
    # exact comparison of every submitted run/source/environment/resource field;
    # admit only these two documented metadata additions.
    check(set(observed_job) == set(job) | {'worker'}, 'unexpected worker job metadata')
    for key, value in job.items():
        if key == 'worker':
            continue  # Assignment is qualified independently below.
        if key == 'config':
            observed_config = observed_job['config']
            check(set(observed_config) == set(value) | {'actual_capacity'}, 'unexpected runtime config metadata')
            check(all(observed_config[name] == submitted for name, submitted in value.items()),
                  'submitted worker environment/resources changed')
            check(observed_config['actual_capacity'] == launched['capacity'],
                  'runtime capacity differs from actual launch')
        else:
            check(observed_job[key] == value, f'immutable job field changed: {key}')
    worker = observed_job['worker']
    check(worker['id'] == assignment['worker_id'] and
          worker['instance_id'] == launched['instance_id'] and worker['reused'] == assignment['reused'],
          'runtime worker assignment differs from launch')
    if 'instance_id' in assignment:
        check(worker['instance_id'] == assignment['instance_id'], 'reused instance differs from controller assignment')
    result = load(results / 'worker-result.json')
    check(result['state'] == 'complete' and result['script_returncode'] == 0, 'worker campaign incomplete')
    check(sha(results / 'source-manifest.json') == manifest_sha, 'worker manifest differs from launch')
    if (results / 'source.tar.gz').exists():
        check(sha(results / 'source.tar.gz') == archive_sha, 'worker source archive differs from launch')
    before, after = load(results / 'source-before.json'), load(results / 'source-after.json')
    check(before == after, 'source changed during diagnostic')
    compact = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    check(before['files'] == len(manifest) and before['content_digest'] == compact, 'runtime complete source digest')
    check(before['source_commit'] == job['source_commit'] and before['worker_job'] == job['id'] and before['cpu'] == '0',
          'runtime source/CPU qualification')
    check(before['worker_manifest_sha256'] == manifest_sha, 'runtime manifest hash')
    check(all(manifest[name] == value for name, value in before['selected_sha256'].items()), 'runtime selected-header identities')
    check(load(results / 'control-provenance.json') == provenance, 'frozen factor provenance differs')
    helpers = {}
    if any(s.get('transformation') == 'nop-prefetch' for s in provenance['factors']):
        check('controls/nop-prefetch.py' in manifest, 'missing captured transformation helper')
        helpers['nop-prefetch'] = manifest['controls/nop-prefetch.py']
    return {'job': job['id'], 'source_commit': job['source_commit'], 'content_digest': job['source']['digest'],
            'source_archive_sha256': archive_sha, 'source_manifest_sha256': manifest_sha,
            'source_files_verified': len(manifest),
            'source_stability': before, 'provenance': provenance, 'transformation_helper_sha256': helpers,
            'run_environment': job['config']['env']}


def check_nop_transform(original, transformed, assembly, transformed_assembly, receipt, helper_sha):
    """Independently reconstruct a PRFM-only edit; never execute artifact code."""
    before, after = Path(original).read_bytes(), Path(transformed).read_bytes()
    recorded = load(receipt)
    check(recorded['format'] == 1 and recorded['transformation'] == 'aarch64-prfm-to-nop'
          and recorded['diagnostic_only'] is True and recorded['machine'] == 'aarch64', 'patch transformation identity')
    check(recorded['input_sha256'] == hashlib.sha256(before).hexdigest()
          and recorded['output_sha256'] == hashlib.sha256(after).hexdigest()
          and recorded['input_bytes'] == recorded['output_bytes'] == len(before) == len(after), 'patch binary identities/sizes')
    check(recorded['helper_sha256'] == helper_sha, 'patch helper differs from frozen source')
    encoded = Path(assembly).read_bytes()
    text = gzip.decompress(encoded) if encoded[:2] == b'\x1f\x8b' else encoded
    check(recorded['disassembly_sha256'] == hashlib.sha256(encoded).hexdigest()
          and recorded['decoded_disassembly_sha256'] == hashlib.sha256(text).hexdigest(), 'patch input assembly identity')
    check(before[:7] == b'\x7fELF\x02\x01\x01' and len(before) >= 64, 'patch ELF class/endianness')
    hdr = struct.unpack_from('<HHIQQQIHHHHHH', before, 16)
    check(hdr[0] in (2, 3) and hdr[1] == 183 and hdr[2] == 1
          and hdr[7:10] == (64, 56, hdr[9]) and 0 < hdr[9] < 65535, 'patch ELF architecture/header layout')
    loads = []
    check(hdr[4] + hdr[8] * hdr[9] <= len(before), 'patch program header bounds')
    for index in range(hdr[9]):
        kind, flags, offset, va, _, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', before, hdr[4] + index * hdr[8])
        if kind == 1 and flags & 1:
            check(filesz <= memsz and offset + filesz <= len(before), 'patch executable mapping bounds')
            loads.append({'program_header_index': index, 'flags': flags, 'file_offset': offset,
                          'virtual_address': va, 'file_bytes': filesz, 'memory_bytes': memsz, 'alignment': align})
    check(loads == recorded['executable_pt_loads'] and recorded['entry_virtual_address'] == hdr[3], 'patch ELF mapping receipt')
    sites = []
    row_pattern = re.compile(r'^\s*([0-9a-fA-F]+):\s*(.*)$')
    for line_number, line in enumerate(text.decode().splitlines(), 1):
        match = row_pattern.match(line)
        if not match:
            continue
        instruction = match.group(2).strip()
        # LLVM raw-word/byte formats and no-show-raw-insn format are accepted.
        instruction = re.sub(r'^(?:[0-9a-fA-F]{8}\s+|(?:[0-9a-fA-F]{2}\s+){4})', '', instruction).strip()
        if not instruction.startswith('prfm\t') and not instruction.startswith('prfm '):
            continue
        va = int(match.group(1), 16)
        mapped = [s for s in loads if s['virtual_address'] <= va and va + 4 <= s['virtual_address'] + s['file_bytes']]
        check(va % 4 == 0 and len(mapped) == 1, 'patch site executable mapping')
        segment = mapped[0]
        offset = segment['file_offset'] + va - segment['virtual_address']
        raw = before[offset:offset + 4]
        word = int.from_bytes(raw, 'little')
        check(offset % 4 == 0 and (word & 0xffc00000 == 0xf9800000
              or word & 0xffe00c00 == 0xf8a00800 or word & 0xff000000 == 0xd8000000), 'patch site is not PRFM opcode')
        sites.append({'virtual_address': va, 'virtual_address_hex': hex(va), 'file_offset': offset,
                      'program_header_index': segment['program_header_index'], 'disassembly_line': line_number,
                      'instruction': 'prfm ' + instruction.split(None, 1)[1],
                      'before_hex': raw.hex(), 'after_hex': '1f2003d5'})
    check(sites and sites == recorded['patches'] and len(sites) == recorded['patch_count'], 'patch receipt omits/adds/misstates instruction sites')
    check(len({s['file_offset'] for s in sites}) == len(sites), 'duplicate patch sites')
    expected = bytearray(before)
    for site in sites:
        expected[site['file_offset']:site['file_offset'] + 4] = bytes.fromhex('1f2003d5')
    check(bytes(expected) == after, 'patch changes bytes beyond exact PRFM-to-NOP replacement')
    out_assembly = Path(transformed_assembly).read_bytes()
    if out_assembly[:2] == b'\x1f\x8b':
        out_assembly = gzip.decompress(out_assembly)
    check(not re.search(r'^\s*[0-9a-fA-F]+:\s*.*\bprfm\b', out_assembly.decode(), re.M), 'patched disassembly retains PRFM')
    return {'transformation': 'aarch64-prfm-to-nop', 'receipt_sha256': sha(receipt),
            'input_sha256': recorded['input_sha256'], 'output_sha256': recorded['output_sha256'],
            'patch_count': len(sites), 'helper_sha256': helper_sha,
            'qualification': 'Exact independent byte reconstruction; executable layout/addresses/branches unchanged.',
            'hint_limit': 'Logical hint targets preserved; executable PRFM instructions removed. No measured traffic claim.'}


def check_capture(results, isa, factor_specs, helper_hashes=None, native_configs=()):
    initial_expected = {'baseline': set(), 'inplace': {'LOOM_ARM_INPLACE_PREPARE=1'},
                'paired': {'LOOM_ARM_INPLACE_PREPARE=1', 'LOOM_ARM_PAIRED_PROBE=1'},
                'single': {'LOOM_ARM_INPLACE_PREPARE=1', 'LOOM_ARM_SINGLE_HANDOFF=1'}}
    targets = {s['name']: s['target'] for s in factor_specs}
    expected = {s['name']: set(s['defines']) if 'defines' in s else initial_expected[s['name']]
                for s in factor_specs}
    commands = load(results / f'compile-{isa}.json')
    configured = [config for config in native_configs if config['isa'] == isa]
    check(not native_configs or len(configured) == 1, 'undeclared or ambiguous native ISA configuration')
    qualifiers = {}
    for factor, target in targets.items():
        spec = next(s for s in factor_specs if s['name'] == factor)
        source_target = target
        if spec.get('transformation'):
            check(spec['transformation'] == 'nop-prefetch' and factor == 'nops' and isa == 'neon', 'unsupported transformed factor')
            check('inplace' in targets and expected[factor] == expected['inplace'], 'NOP factor source macro reference changed')
            source_target = targets['inplace']
        selected = [c for c in commands if c['file'].endswith('/table_ring.cpp') and
                    (f'{source_target}.dir/' in c.get('output', '') or f'{source_target}.dir/' in c.get('command', ''))]
        check(len(selected) == 1, f'compile identity {factor}')
        command = shlex.split(selected[0]['command'])
        macros = {v[2:] for v in command if v.startswith('-DLOOM_ARM_')}
        check(macros == expected[factor], f'causal factor defines {factor}: {macros}')
        for flag in ('-O3', '-std=c++23', '-DNDEBUG', '-fno-vectorize', '-fno-slp-vectorize',
                     '-fno-fast-math', '-ffp-contract=off'):
            check(flag in command, f'compile qualifier {factor}/{flag}')
        if isa == 'neon':
            check('-march=armv8-a+simd' in command and '-mtune=neoverse-v2' in command, f'native ARM flags {factor}')
        if configured:
            config = configured[0]
            compiler_tune = {'zen5':'znver5', 'granite-rapids':'graniterapids'}.get(config['tune'], config['tune'])
            check('-march=' + config['march'] in command and '-mtune=' + compiler_tune in command,
                  f'frozen native ISA/tuning flags {factor}')
        qualifiers[factor] = selected[0]
    binary_record = {}
    for line in (results / f'binary-sha256-{isa}.txt').read_text().splitlines():
        digest, name = line.split(None, 1)
        binary_record[Path(name.strip()).name] = digest
    identities = {}
    for factor, target in targets.items():
        binary = results / f'table-ring-{isa}-{factor}'
        check(sha(binary) == binary_record[target], f'captured binary hash {factor}')
        elf_header = binary.read_bytes()[:64]
        check(elf_header[:7] == b'\x7fELF\x02\x01\x01' and
              struct.unpack_from('<H', elf_header, 18)[0] == (183 if isa == 'neon' else 62),
              f'native ELF architecture {factor}')
        identities[factor] = {'binary_sha256': sha(binary),
                              'assembly_gzip_sha256': sha(results / f'assembly-{isa}-{factor}.txt.gz')}
    for spec in factor_specs:
        if spec.get('transformation'):
            check(helper_hashes and 'nop-prefetch' in helper_hashes, 'missing frozen patch-helper identity')
            identities[spec['name']]['transformation'] = check_nop_transform(
                results / f'table-ring-{isa}-inplace', results / f'table-ring-{isa}-{spec["name"]}',
                results / f'patch-input-{isa}.asm', results / f'assembly-{isa}-{spec["name"]}.txt.gz',
                results / f'nop-prefetch-{isa}.json', helper_hashes['nop-prefetch'])
    plan_sha = sha(results / f'table-plans-{isa}.h')
    check(plan_sha == '28e8aff744d0c7e8bb4653f2e515ae1ee0fb9b06a6a69dbe673ee4e5ac6d24d3', 'generated plan changed')
    check((results / f'plans-sha256-{isa}.txt').read_text().split()[0] == plan_sha, 'generated plan receipt')
    validation = (results / f'loom-validation-{isa}.txt').read_text()
    check('100% tests passed, 0 tests failed out of 16' in validation, 'native Loom16 qualification incomplete')
    compiler = (results / 'compiler.txt').read_text()
    check('clang version 21.1.8' in compiler, 'compiler pin')
    return {'compiler_version_sha256': sha(results / 'compiler.txt'),
            'compiler_executable_receipt_sha256': sha(results / 'compiler-sha256.txt'),
            'compile_database_sha256': sha(results / f'compile-{isa}.json'),
            'compile_qualifiers': qualifiers, 'binaries': identities, 'generated_plan_sha256': plan_sha,
            'libraries_receipt_sha256': sha(results / f'library-sha256-{isa}.txt')}


def references(original):
    return {case[0]: check_cell(original / (case[0] + '.csv'), original / (case[0] + '.log'), case, 7)
            for case in CASES}


def analyze(results, launch, original, isa='neon', reps=None, passes=None):
    source = check_source(results, launch)
    provenance, environment = source['provenance'], source['run_environment']
    parameter_names = ('REPS', 'PASSES') if 'parameter_environment' in provenance else ('ARM_SERIAL_REPS', 'ARM_SERIAL_PASSES')
    expected_reps = int(environment.get(parameter_names[0], provenance['default_repetitions']))
    expected_passes = int(environment.get(parameter_names[1], provenance['default_passes']))
    check(expected_reps > 0 and expected_passes in (1, 2), 'invalid frozen repetition/pass parameters')
    reps = expected_reps if reps is None else reps
    passes = expected_passes if passes is None else passes
    check((reps, passes) == (expected_reps, expected_passes), 'CLI repetition/pass override differs from frozen run environment')
    factor_specs = provenance['factors']
    factors_order = tuple(spec['name'] for spec in factor_specs)
    targets = {spec['name']: spec['target'] for spec in factor_specs}
    check(len(factors_order) == len(set(factors_order)) and len(targets) == len(factor_specs), 'duplicate factor names')
    reference_factor = provenance.get('comparison_reference', factors_order[0])
    check(reference_factor in factors_order, 'invalid reference factor')
    case_specs = provenance['case_order']
    check(case_specs and len({case['name'] for case in case_specs}) == len(case_specs), 'empty/duplicate cases')
    case_by_name = {case['name']: case for case in case_specs}
    for case in case_specs:
        check(case['entries'] >= 32 and case['concurrency'] in (1, 8, 32) and case['turnovers'] >= 0
              and case['mixed'] in (0, 1) and case['a'] >= 3, 'invalid frozen case parameters')
        check(set(case['factor_order']) == set(factors_order) and len(case['factor_order']) == len(factors_order), 'case factor order')
    capture = check_capture(results, isa, factor_specs, source['transformation_helper_sha256'],
                            provenance.get('supported_native_configs', ()))
    # Historical NEON counters are only a like-for-like check for the original
    # four aged cells. Fresh and other ISAs use independent law plus current-run
    # equality, rather than borrowing unrelated physical/hint counters.
    original_cells = references(original) if isa == 'neon' else {}
    forced_case = ('forced', 5, 701, 8)
    forced_reference = (check_cell(original / 'check.csv', original / 'check.log', forced_case, 1, generation=2)
                        if isa == 'neon' else None)
    forced = {}
    for factor in factors_order:
        checked = check_cell(results / f'check-{isa}-{factor}.csv', results / f'check-{isa}-{factor}.context',
                             forced_case, 1, reference=forced_reference, generation=2)
        forced[factor] = checked
    forced_ref = forced[reference_factor]
    check(all(value['physical'] == forced_ref['physical'] and value['frame_dimensions'] == forced_ref['frame_dimensions']
              for value in forced.values()), 'forced factor work/frame dimensions differ')
    order_rows = list(csv.DictReader((results / 'executable-order.csv').open()))
    expected_order = []
    for case in case_specs:
        for pass_index in range(passes):
            order = case.get('pass_orders', [case['factor_order'], case['factor_order'][::-1]])[pass_index]
            check(set(order) == set(factors_order) and len(order) == len(factors_order), 'pass factor order')
            expected_order.extend((case['name'], pass_index, index, factor) for index, factor in enumerate(order))
    actual_order = [(row['case'], int(row['pass']), int(row['order']), row['factor']) for row in order_rows]
    check(actual_order == expected_order, 'factor/case executable order')
    cells, case_physical, case_frames = [], {}, {}
    for spec in case_specs:
        name = spec['name']
        case = (name, spec['a'], spec['entries'], spec['concurrency'])
        historical = original_cells.get(name) if spec['turnovers'] == 8 and spec['mixed'] == 1 else None
        for pass_index in range(passes):
            factors = {}
            # Read the declared reference first, independent of execution order,
            # to derive the current ISA's observed frame and Ring dimensions.
            read_order = (reference_factor,) + tuple(f for f in factors_order if f != reference_factor)
            for factor in read_order:
                stem = results / f'{isa}-{name}-{factor}-pass{pass_index}'
                checked = check_cell(stem.with_suffix('.csv'), stem.with_suffix('.context'), case, reps,
                                     reference=historical, generation=spec['turnovers'], mixed=spec['mixed'],
                                     frame_reference=case_frames.get(name))
                checked['factor'] = factor
                policy = load(stem.with_suffix('.policy.json'))
                check(policy['csv_sha256'] == checked['csv_sha256'] and
                      policy['context_sha256'] == checked['context_sha256'], f'runtime row/policy receipt: {stem}')
                check(policy['parameters'] == {'entries':spec['entries'], 'concurrency':spec['concurrency'],
                      'repetitions':reps, 'turnovers':spec['turnovers'], 'mixed':spec['mixed'], 'seed':37913},
                      f'runtime case parameter receipt: {stem}')
                if name not in case_physical:
                    case_physical[name], case_frames[name] = checked['physical'], checked['frame_dimensions']
                check(checked['physical'] == case_physical[name], f'physical/stage/hint work differs across factors/passes: {stem}')
                factors[factor] = checked
            check(len({value['context']['hardware_context'] for value in factors.values()}) == 1, f'hardware differs by factor {name}')
            baseline = factors[reference_factor]['medians_ns']
            for factor, value in factors.items():
                median = value['medians_ns']
                value['comparisons'] = {
                    'ring_hints_over_baseline_ring_hints': median['ring_hints'] / baseline['ring_hints'],
                    'ring_hints_minus_baseline_ring_hints_ns': median['ring_hints'] - baseline['ring_hints'],
                    'ring_hints_over_fixed_baseline_manual_hints': median['ring_hints'] / baseline['manual_hints'],
                    'manual_hints_over_baseline_manual_hints': median['manual_hints'] / baseline['manual_hints'],
                    'ring_over_baseline_ring': median['ring'] / baseline['ring']}
                if historical is not None:
                    value['comparisons']['historical_ring_hints_ratio'] = median['ring_hints'] / historical['medians_ns']['ring_hints']
            cells.append({'case':name, 'pass':pass_index, 'reference_factor':reference_factor, 'factors':factors})
    for row in order_rows:
        case = case_by_name[row['case']]
        check((row['isa'], row['target'], int(row['a']), int(row['entries']), int(row['turnovers']), int(row['mixed']),
               int(row['concurrency']), int(row['reps']), int(row['seed'])) ==
              (isa, targets[row['factor']], case['a'], case['entries'], case['turnovers'], case['mixed'], case['concurrency'], reps, 37913),
              'execution-order qualifiers')
        check(datetime.datetime.fromisoformat(row['end_utc'].replace('Z', '+00:00')) >=
              datetime.datetime.fromisoformat(row['start_utc'].replace('Z', '+00:00')), 'executable timestamp order')
    hardware = load(results / f'hardware-{isa}.json')
    check(not hardware['calibration_enabled'] and hardware['reported']['selected_cpu'] == 0, 'hardware inspection CPU/calibration')
    all_cells = [value for cell in cells for value in cell['factors'].values()] + list(forced.values())
    check(all(value['context']['hardware_context'] == hardware['behavior_context'] for value in all_cells), 'timing/inspection hardware context mismatch')
    check(all(int(value['context']['policy_revision']) == hardware['policy']['revision']
              and int(value['context']['line_bytes']) == hardware['policy']['line_bytes']
              and int(value['context']['page_bytes']) == hardware['policy']['page_bytes'] for value in all_cells), 'timing/inspection policy geometry mismatch')
    merged = {}
    for name in case_by_name:
        merged[name] = {}
        for factor in factors_order:
            samples = {arm: [sample for cell in cells if cell['case'] == name
                            for sample in cell['factors'][factor]['samples_ns'][arm]] for arm in ARMS}
            medians = {arm:statistics.median(samples[arm]) for arm in ARMS}
            merged[name][factor] = {'samples_ns':samples, 'medians_ns':medians,
                                   'own_manual_hints_gap_ns':medians['ring_hints'] - medians['manual_hints'],
                                   'own_manual_hints_ratio':medians['ring_hints'] / medians['manual_hints']}
        baseline = merged[name][reference_factor]['medians_ns']
        for factor in factors_order:
            medians = merged[name][factor]['medians_ns']
            merged[name][factor]['fixed_reference_manual_hints_gap_ns'] = medians['ring_hints'] - baseline['manual_hints']
            merged[name][factor]['ring_hints_over_reference_ring_hints'] = medians['ring_hints'] / baseline['ring_hints']
    limits = ['Historical ratios compare separate captures; fixed-baseline comparisons use this run.',
              'Fresh counters and other-ISA counters are qualified independently and across this run, not copied from historical aged NEON.',
              'Terminal keys and per-request answers are checked inside captured executable; absent from CSV.',
              'Stage/hint counters are logical; no PMU stalls/IPC or measured cache-source/traffic claim.']
    if reps < 7:
        limits.append(f'{reps} repetitions per factor/pass; this is a reduced panel.')
    if passes == 1:
        limits.append('One pass counterbalances factor order across different cases, not within the same case.')
    return {'status':'qualified_native_capture', 'isa':isa, 'reference_factor':reference_factor,
            'factor_order':factors_order, 'repetitions':reps, 'passes':passes,
            'timing_rows':7 * reps * len(case_specs) * len(factors_order) * passes,
            'factor_case_cells':len(case_specs) * len(factors_order) * passes,
            'source':source, 'capture':capture, 'forced_cells':forced, 'cells':cells, 'merged':merged,
            'historical_reference_cells':original_cells, 'observed_frame_dimensions':case_frames, 'limits':limits}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', nargs='?', type=Path)
    parser.add_argument('--launch', type=Path, default=Path('build/workers') / INITIAL_JOB)
    parser.add_argument('--original', type=Path, default=Path('build/loom-ring-review-20261013/review/ikea-table-ring-neon-serial-20261013/recovered'))
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--isa', default='neon')
    parser.add_argument('--reps', type=int)
    parser.add_argument('--passes', type=int)
    args = parser.parse_args()
    if args.self_test:
        ref = references(args.original)
        forced = check_cell(args.original / 'check.csv', args.original / 'check.log',
                            ('forced', 5, 701, 8), 1, generation=2)
        with tempfile.TemporaryDirectory(prefix='loom-arm-analysis-self-test-') as scratch:
            synthetic = Path(scratch) / 'wrong-concurrency.csv'
            with (args.original / 'hot_serial.csv').open() as source:
                reader = csv.DictReader(source)
                fields, rows = reader.fieldnames, list(reader)
            rows[0]['concurrency'] = '32'
            with synthetic.open('w', newline='') as out:
                writer = csv.DictWriter(out, fieldnames=fields)
                writer.writeheader()
                writer.writerows(rows)
            rejected = False
            try:
                check_cell(synthetic, args.original / 'hot_serial.log', CASES[0], 7)
            except ValueError:
                rejected = True
        check(rejected, 'wrong-concurrency negative control was accepted')
        job, manifest, _ = check_frozen_source(args.launch)
        result = {'status': 'offline_self_test_passed', 'original_cells': ref, 'forced_reference': forced,
                  'wrong_concurrency_rejected': True, 'frozen_source_files_verified': len(manifest),
                  'frozen_job_verified': job['id'], 'script_sha256': sha(__file__)}
    else:
        check(args.results is not None, 'results directory required')
        result = analyze(args.results, args.launch, args.original, args.isa, args.reps, args.passes)
        result['script_sha256'] = sha(__file__)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'output': str(args.output),
                      'timing_rows': result.get('timing_rows')}))


if __name__ == '__main__':
    main()
