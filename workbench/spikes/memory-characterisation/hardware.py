"""Reported facts and conservative CPU recognition; no inferred performance constants."""
from __future__ import annotations
import hashlib
import json
import os
import platform
from pathlib import Path


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def cpus(value):
    result = set()
    for part in (value or '').split(','):
        if not part:
            continue
        pair = [int(x) for x in part.split('-')]
        result.update(range(pair[0], pair[-1] + 1))
    return sorted(result)


def discover(cpu):
    allowed = sorted(os.sched_getaffinity(0))
    if cpu not in allowed:
        raise ValueError(f'CPU {cpu} not in allowed affinity {allowed}')
    blocks = [dict(line.split(':', 1) for line in block.splitlines() if ':' in line)
              for block in Path('/proc/cpuinfo').read_text().split('\n\n')]
    blocks = [{k.strip(): v.strip() for k, v in block.items()} for block in blocks]
    info = next((block for block in blocks if block.get('processor') == str(cpu)), blocks[0])
    identity = {k: info.get(k) for k in ['vendor_id', 'cpu family', 'model', 'stepping',
        'model name', 'microcode', 'CPU implementer', 'CPU architecture', 'CPU variant',
        'CPU part', 'CPU revision']}
    identity['isa'] = platform.machine()
    features = (info.get('flags') or info.get('Features') or '').split()
    identity['hypervisor_flag'] = 'hypervisor' in features
    root = Path(f'/sys/devices/system/cpu/cpu{cpu}')
    identity['midr'] = read(root / 'regs/identification/midr_el1')
    family, model, vendor = identity['cpu family'], identity['model'], identity['vendor_id']
    # Name only. Behavioural values require matching evidence, never this label.
    known = {('AuthenticAMD', '23', '49'): 'Zen 2', ('AuthenticAMD', '25', '1'): 'Zen 3', ('AuthenticAMD', '25', '17'): 'Zen 4',
             ('AuthenticAMD', '26', '2'): 'Zen 5', ('GenuineIntel', '6', '106'): 'Ice Lake',
             ('GenuineIntel', '6', '143'): 'Sapphire Rapids', ('GenuineIntel', '6', '173'): 'Granite Rapids',
             ('GenuineIntel', '6', '85'): 'Skylake/Cascade Lake'}
    name = known.get((vendor, family, model))
    if identity['CPU implementer'] == '0x41':
        name = {'0xd0c': 'Neoverse N1', '0xd40': 'Neoverse V1', '0xd4f': 'Neoverse V2'}.get(identity['CPU part'])
    caches = []
    for index in sorted((root / 'cache').glob('index*')):
        caches.append({k: read(index / k) for k in ['level', 'type', 'size', 'coherency_line_size',
            'ways_of_associativity', 'number_of_sets', 'shared_cpu_list', 'id']})
    lines = {int(c['coherency_line_size']) for c in caches
             if c['type'] in ('Data', 'Unified') and c['coherency_line_size']}
    peers = []
    siblings = cpus(read(root / 'topology/thread_siblings_list'))
    for other in allowed:
        if other == cpu:
            continue
        shared = [c['level'] for c in caches if other in cpus(c['shared_cpu_list'])]
        peers.append({'cpu': other, 'relation': 'smt-sibling' if other in siblings else 'separate-core',
                      'shared_cache_levels': shared,
                      'numa_nodes': [p.name for p in Path(f'/sys/devices/system/cpu/cpu{other}').glob('node*')],
                      'package': read(f'/sys/devices/system/cpu/cpu{other}/topology/physical_package_id')})
    thp = Path('/sys/kernel/mm/transparent_hugepage')
    pages = {'base_bytes': os.sysconf('SC_PAGE_SIZE'), 'thp_enabled': read(thp / 'enabled'),
             'thp_defrag': read(thp / 'defrag'), 'thp_sizes': {}, 'hugetlb': {}}
    for folder in sorted(thp.glob('hugepages-*kB')):
        pages['thp_sizes'][folder.name] = read(folder / 'enabled')
    for folder in sorted(Path('/sys/kernel/mm/hugepages').glob('hugepages-*kB')):
        pages['hugetlb'][folder.name] = {k: read(folder / k) for k in
            ['nr_hugepages', 'free_hugepages', 'resv_hugepages', 'surplus_hugepages']}
    result = {'identity': identity, 'recognised_architecture': name,
        'recognition': 'named-architecture' if name else 'unknown-model',
        'kernel': platform.release(), 'cpu': cpu, 'allowed_cpus': allowed,
        'thread_siblings': siblings, 'peers': peers, 'caches': caches, 'pages': pages,
        'line_bytes': min(lines) if lines else None, 'cache_line_source': 'linux-sysfs' if lines else 'unknown',
        'features': features, 'numa_nodes': [p.name for p in root.glob('node*')],
        'governor': read(root / 'cpufreq/scaling_governor'),
        'perf_event_paranoid': read('/proc/sys/kernel/perf_event_paranoid')}
    result['numa'] = {p.name: {k: read(p / k) for k in ['cpulist', 'distance']}
                      for p in sorted(Path('/sys/devices/system/node').glob('node[0-9]*'))}
    # Ignore CPU numbering and current free hugepage counts, but preserve cache
    # geometry/sharing widths, SMT visibility, kernel, microcode and page policy.
    key = {'identity': identity, 'kernel': result['kernel'], 'base_page': pages['base_bytes'],
           'thp_policy': [pages['thp_enabled'], pages['thp_defrag'], pages['thp_sizes']],
           'caches': [{k: c[k] for k in ['level', 'type', 'size', 'coherency_line_size', 'ways_of_associativity']} |
                      {'shared_cpus': len(cpus(c['shared_cpu_list']))} for c in caches],
           'smt_width': len(siblings), 'governor': result['governor']}
    result['lookup_context'] = key
    result['lookup_key'] = hashlib.sha256(json.dumps(key, sort_keys=True).encode()).hexdigest()
    return result
