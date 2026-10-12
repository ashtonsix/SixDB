#!/usr/bin/env python3
"""Compare the native cold report with independent Linux/spike discovery."""
import argparse
import json
import os
from pathlib import Path
import platform
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'workbench/spikes/memory-characterisation'))
from hardware import cpus, discover, read


def integer(value):
    return None if value is None else int(value, 16 if value.startswith('0x') else 10)


def capacity(value):
    if value is None:
        return None
    multiplier = {'K': 1024, 'M': 1024**2, 'G': 1024**3, 'T': 1024**4}.get(value[-1], 1)
    return int(value[:-1] if multiplier != 1 else value) * multiplier


def verify(path):
    report = json.loads(path.read_text())
    p = report['reported']
    assert p['allowed_cpus'] == sorted(os.sched_getaffinity(0))
    assert p['selected_cpu'] in p['allowed_cpus']
    assert p['isa'] == platform.machine() and p['kernel'] == platform.release()
    assert [c['cpu'] for c in p['cpus']] == p['allowed_cpus']
    assert p['pages']['base_bytes'] == os.sysconf('SC_PAGE_SIZE')
    checked = 0
    for c in p['cpus']:
        h = discover(c['cpu'])
        identity = h['identity']
        for native, linux in [('vendor', 'vendor_id'), ('model_name', 'model name'), ('microcode', 'microcode')]:
            assert c['identity'][native] == identity[linux], (c['cpu'], native)
        for native, linux in [('family', 'cpu family'), ('model', 'model'), ('stepping', 'stepping'),
                              ('implementer', 'CPU implementer'), ('architecture', 'CPU architecture'),
                              ('variant', 'CPU variant'), ('part', 'CPU part'), ('revision', 'CPU revision'), ('midr', 'midr')]:
            assert c['identity'][native] == integer(identity[linux]), (c['cpu'], native)
        assert c['advertised_features'] == sorted(set(h['features']))
        assert c['identity']['hypervisor_advertised'] == ('hypervisor' in h['features'])
        assert c['thread_siblings'] == h['thread_siblings']
        assert c['numa_nodes'] == sorted(int(n[4:]) for n in h['numa_nodes'])
        root = f"/sys/devices/system/cpu/cpu{c['cpu']}"
        for native, linux in [('core_id', 'core_id'), ('package_id', 'physical_package_id'), ('die_id', 'die_id')]:
            value = read(f'{root}/topology/{linux}')
            # Linux -1 means no reported topology identifier.
            expected = None if value is None or value.startswith('-') else integer(value)
            assert c[native] == expected, (c['cpu'], native)
        package = read(f'{root}/topology/core_siblings_list')
        assert c['package_cpus'] == (None if package is None else cpus(package))
        assert c['cache_directory_reported'] and len(c['caches']) == len(h['caches'])
        for native, raw in zip(c['caches'], h['caches']):
            assert native['type'] == raw['type'].lower()
            assert native['capacity_bytes'] == capacity(raw['size'])
            for name, source in [('id', 'id'), ('level', 'level'), ('line_bytes', 'coherency_line_size'),
                                 ('ways', 'ways_of_associativity'), ('sets', 'number_of_sets')]:
                value = integer(raw[source])
                if name in ('ways', 'sets', 'line_bytes', 'level') and value == 0:
                    value = None
                assert native[name] == value, (c['cpu'], name)
            assert native['shared_cpus'] == cpus(raw['shared_cpu_list'])
        checked += 1
    policy = report['policy']
    observations = report['observations']
    if report['calibration_enabled']:
        assert observations['local_attempted'] or policy['state'] == 'lookup'
    else:
        assert not observations['local_attempted']
    fields = [observations['width'], observations['training'], *observations['adjacent_forward']]
    for field in fields:
        if field['origin'] == 'local':
            assert field['scope']['method_version'] == 2
            assert field['scope']['interference'] == 'foreground_conditional'
            if field['scope']['measurement_cpu'] != -1:
                assert field['scope']['measurement_cpu'] == p['selected_cpu']
        if field['origin'] in ('provisional', 'supplied'):
            assert field['scope']['page_mode'] == 'unknown'
    for receipt in observations['mapping_receipts']:
        if receipt['smaps_known']:
            assert receipt['kernel_page_bytes'] > 0 and receipt['mmu_page_bytes'] > 0
        if receipt['numa_known']:
            assert receipt['smaps_known'] and receipt['residence']
            assert len({n['node'] for n in receipt['residence']}) == len(receipt['residence'])
            assert sum(n['pages'] for n in receipt['residence']) * receipt['kernel_page_bytes'] == receipt['mapping_bytes']
    print(f'PASS: {checked} CPU inventories match independent Linux discovery; cold policy/observation scope verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    verify(parser.parse_args().report)
