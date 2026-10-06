"""Focused ownership and physical-progress seams; no broad panel replay."""


def panel(hw, quick=False):
    cases = []
    first = str(hw['physical_cpus'][0])

    def add(name, target, args):
        cases.append({'name': name, 'target': target, 'args': list(map(str, args))})

    # Keep the previous warm native shape as a source-change control. Short
    # jobs below include admission/retirement rather than amortizing them away.
    add('seam-warm-control', 'loom_microtask',
        ['--workload', 'chains', '--variant', 'ring', '--ring', 16, '--overflow', 0,
         '--jobs', 16, '--steps', 4096 if quick else 262144, '--dataset-items', 128,
         '--heterogeneous', '--cpus', first, '--typed-chains', '--hint-path', 'specialized',
         '--hint-class', 'none', '--prefetch', 'none'])
    waves = 8 if quick else 256
    for variant in ['shared', 'parent-banks', 'family-banks']:
        for steps in [1, 2, 8, 64]:
            add(f'seam-short-{variant}-s{steps}', 'loom_inventory',
                ['--case', 'short', '--variant', variant, '--jobs-a', 32, '--jobs-b', 8,
                 '--steps-a', steps, '--steps-b', steps, '--slots', 8, '--overflow', 0,
                 '--credits', 8, '--visits', 128, '--waves', waves, '--cpus', first])
        for admission in ['fifo', 'reserve-b']:
            add(f'seam-two-parent-{variant}-{admission}', 'loom_inventory',
                ['--case', 'two-parent', '--variant', variant, '--admission', admission,
                 '--jobs-a', 64, '--jobs-b', 8, '--steps-a', 1024, '--steps-b', 2,
                 '--slots', 8, '--overflow', 8, '--credits', 16, '--visits', 16,
                 '--waves', 2 if quick else 32, '--cpus', first])
        for cancellation in ['selective', 'discard', 'drain']:
            add(f'seam-cancel-{variant}-{cancellation}', 'loom_inventory',
                ['--case', 'two-parent', '--variant', variant, '--admission', 'reserve-b',
                 '--cancel-mode', cancellation, '--cancel-a-after', 4,
                 '--jobs-a', 64, '--jobs-b', 8, '--steps-a', 1024, '--steps-b', 2,
                 '--slots', 8, '--overflow', 8, '--credits', 16, '--visits', 16,
                 '--waves', 2 if quick else 32, '--cpus', first])
    for variant in ['shared', 'family-banks']:
        add(f'seam-underfill-{variant}', 'loom_inventory',
            ['--case', 'short', '--variant', variant, '--jobs-a', 3, '--jobs-b', 1,
             '--steps-a', 2, '--steps-b', 2, '--slots', 8, '--overflow', 0,
             '--credits', 8, '--visits', 1, '--waves', waves, '--cpus', first])
    for size in [8, 64]:
        add(f'seam-cleanup-{size}', 'loom_inventory',
            ['--case', 'cleanup', '--variant', 'shared', '--jobs-a', size, '--jobs-b', size,
             '--steps-a', 8, '--steps-b', 2, '--slots', size, '--overflow', size,
             '--credits', 2 * size, '--waves', 8 if quick else 1024, '--cpus', first])
    for placement, cpus in hw['pair_placements']:
        pair = ','.join(map(str, cpus))
        for buffers, hold in [(1, 100), (2, 0), (2, 100), (8, 100)]:
            add(f'seam-pressure-{placement}-reserved-b{buffers}-h{hold}', 'loom_pressure',
                ['--variant', 'reserved', '--jobs', 16 if quick else 64,
                 '--buffers', buffers, '--elements', 257, '--grain', 63,
                 '--work-capacity', 2, '--hold-us', hold, '--cpus', pair])
        add(f'seam-pressure-{placement}-unified-negative', 'loom_pressure',
            ['--variant', 'unified', '--jobs', 64, '--buffers', 2,
             '--elements', 257, '--grain', 63, '--work-capacity', 2,
             '--hold-us', 1000, '--stall-ms', 1, '--cpus', pair])
        for variant in ['reserved', 'unified']:
            add(f'seam-pressure-{placement}-{variant}-cancel', 'loom_pressure',
                ['--variant', variant, '--jobs', 64, '--buffers', 2,
                 '--elements', 257, '--grain', 63, '--work-capacity', 2,
                 '--hold-us', 1000, '--cancel-while-using', '--cpus', pair])
    return cases
