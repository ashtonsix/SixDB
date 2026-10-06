"""Matched follow-up cuts after the hot-path and observer audit."""


def panel(hw, quick=False):
    cases = []
    physical = hw['physical_cpus']
    first = str(physical[0])

    def add(name, target, args):
        cases.append({'name': name, 'target': target, 'args': list(map(str, args))})

    # The full ten-scalar preserve_none indirect tail is a retained Clang 21
    # register-allocation rejection on x86. Do not shrink its payload to time it.
    abi_targets = (['loom_abi_ordinary', 'loom_abi_regcall']
                   if hw['cpus'][0]['identity']['isa'] == 'x86_64'
                   else ['loom_abi', 'loom_abi_ordinary'])
    for target in abi_targets:
        for vectors in [0, 2, 8, 12]:
            for variant in ['direct', 'manual', 'ring']:
                add(f'confirm-abi-{target}-{variant}-v{vectors}', target,
                    ['--cpus', first, '--variant', variant, '--ring', 16, '--jobs', 16,
                     '--steps', 4096 if quick else 262144, '--vectors', vectors, '--heterogeneous'])

    for path in ['legacy', 'specialized', 'runtime']:
        for kind, policy in [('none', 'none'), ('l1-1', 'first')]:
            for variant in ['direct', 'manual', 'ring']:
                add(f'confirm-warm-{path}-{kind}-{variant}', 'loom_microtask',
                    ['--workload', 'chains', '--variant', variant, '--ring', 16,
                     '--overflow', 0, '--jobs', 16, '--steps', 4096 if quick else 262144,
                     '--dataset-items', 128, '--heterogeneous', '--cpus', first,
                     '--typed-chains',
                     '--hint-path', path, '--hint-class', kind, '--prefetch', policy])
                if variant == 'ring':
                    add(f'confirm-warm-vector-{path}-{kind}', 'loom_microtask',
                        ['--workload', 'chains', '--variant', 'ring', '--ring', 16,
                         '--overflow', 0, '--jobs', 16, '--steps', 4096 if quick else 262144,
                         '--dataset-items', 128, '--heterogeneous', '--vectors', '--cpus', first,
                         '--typed-chains',
                         '--hint-path', path, '--hint-class', kind, '--prefetch', policy])
        for variant in ['direct', 'manual', 'ring']:
            add(f'confirm-chain-{path}-{variant}', 'loom_microtask',
                ['--workload', 'chains', '--variant', variant, '--ring', 32,
                 '--overflow', 128, '--jobs', 128, '--steps', 256,
                 '--dataset-items', 2048 if quick else 4194304, '--cpus', first,
                 '--typed-chains',
                 '--hint-path', path, '--hint-class', 'l1-1', '--prefetch', 'first'])
            add(f'confirm-bec-{path}-{variant}', 'loom_microtask',
                ['--workload', 'bec256', '--variant', variant, '--ring', 16,
                 '--overflow', 128, '--jobs', 128, '--steps', 256,
                 '--dataset-items', 2048 if quick else 1048576, '--cpus', first,
                 '--hint-path', path, '--hint-class', 'l1-1', '--prefetch', 'first'])
        for grain in [1, 1024]:
            add(f'confirm-pages-{path}-g{grain}', 'loom_microtask',
                ['--workload', 'ranges', '--variant', 'ring', '--ring', 16,
                 '--overflow', 128, '--jobs', 128, '--range-bytes', 65536,
                 '--grain', grain, '--cpus', first, '--hint-path', path,
                 '--hint-class', 'page8', '--prefetch', 'pages'])
    # Conservative qualification keeps all hints within the retained windows.
    # Chains bind 64B, so multi-line requests are downgrade controls.
    for kind in ['l2-1', 'l1-2', 'l1-l2-2']:
        add(f'confirm-chain-class-{kind}', 'loom_microtask',
            ['--workload', 'chains', '--variant', 'ring', '--ring', 32,
             '--overflow', 128, '--jobs', 128, '--steps', 256,
             '--dataset-items', 2048 if quick else 4194304, '--cpus', first,
             '--typed-chains',
             '--hint-path', 'specialized', '--hint-class', kind])
    # A 16-line region qualifies every line pattern here. These cases actually
    # increase advisory traffic, unlike a downgraded 64B chain request.
    for kind in ['none', 'l1-1', 'l2-1', 'l1-2', 'l1-l2-2']:
        add(f'confirm-line-class-{kind}', 'loom_microtask',
            ['--workload', 'ranges', '--variant', 'ring', '--ring', 16,
             '--overflow', 128, '--jobs', 128, '--range-bytes', 65536,
             '--grain', 16, '--cpus', first, '--hint-path', 'specialized', '--hint-class', kind])
    for kind, policy in [('none', 'none'), ('l1-1', 'first')]:
        add(f'confirm-warm-generic-legacy-{kind}', 'loom_microtask',
            ['--workload', 'chains', '--variant', 'ring', '--ring', 16,
             '--overflow', 0, '--jobs', 16, '--steps', 4096 if quick else 262144,
             '--dataset-items', 128, '--heterogeneous', '--cpus', first,
             '--hint-path', 'legacy', '--hint-class', kind, '--prefetch', policy])

    for placement, cpus in hw['pair_placements']:
        pair = ','.join(map(str, cpus))
        for variant in ['sparse', 'inbox']:
            for sample in [0, 64, 1]:
                args = ['--variant', variant, '--workers', 2, '--group-size', 2,
                        '--pattern', 'pingpong', '--capacity', 64,
                        '--messages', 2000 if quick else 20000, '--retry',
                        '--duration-ms', 1000, '--drain-ms', 1000, '--cpus', pair]
                if sample:
                    args += ['--instrumented', '--sample-every', sample]
                add(f'confirm-observer-{placement}-{variant}-sample{sample}', 'loom_messaging', args)
        for visits in [16, 128, 1024]:
            for observer in ['full', 'reduced']:
                add(f'confirm-service-{placement}-v{visits}-{observer}', 'loom_service',
                    ['--cpus', pair, '--visits', visits, '--budget-ns', 0,
                     '--service-observer', observer, '--jobs', 32,
                     '--steps', 4096 if quick else 32768,
                     '--dataset-items', 2048 if quick else 4194304,
                     '--messages', 100 if quick else 1000, '--interval-ns', 100000])
        for distribution in ['uniform', 'skew']:
            for uses in [1, 8, 16]:
                for variant in ['owner', 'shared', 'copy']:
                    add(f'confirm-reuse-{placement}-{distribution}-{variant}-u{uses}', 'loom_affinity',
                        ['--family', 'join', '--variant', variant, '--cpus', pair,
                         '--build-rows', 4096 if quick else 2097152,
                         '--probe-rows', 2048 if quick else 262144, '--uses', uses,
                         '--distribution', distribution, '--output-cap', 4096])

    sizes = sorted(set(n for n in [8, len(physical)] if 2 <= n <= len(physical)))
    if quick:
        sizes = sizes[:1]
    for n in sizes:
        cpus = ','.join(map(str, physical[:n]))
        for variant in ['dense', 'sparse', 'inbox', 'group-direct', 'group-routed']:
            capacities = [8, 8 * (n - 1)] if variant == 'inbox' else [8]
            for capacity in capacities:
                for pause in [0, 5000]:
                    add(f'confirm-credit-n{n}-{variant}-c{capacity}-pause{pause}', 'loom_messaging',
                        ['--variant', variant, '--workers', n, '--group-size', 4,
                         '--pattern', 'fanin', '--receiver-only', '--capacity', capacity,
                         '--messages', 1000 if quick else 10000, '--retry',
                         '--duration-ms', 3000, '--drain-ms', 1000,
                         '--pause-worker', 0, '--pause-after', 0, '--pause-us', pause,
                         '--instrumented', '--sample-every', 64, '--cpus', cpus])
            add(f'confirm-skew-n{n}-{variant}', 'loom_messaging',
                ['--variant', variant, '--workers', n, '--group-size', 4,
                 '--pattern', 'all-to-all', '--skew', 90, '--capacity', 64,
                 '--messages', 1000 if quick else 10000, '--retry',
                 '--duration-ms', 3000, '--drain-ms', 1000,
                 '--instrumented', '--sample-every', 64, '--cpus', cpus])
        for variant in ['ttas', 'backoff', 'mcs', 'useful', 'owner']:
            add(f'confirm-single-writer-{variant}-n{n}', 'loom_affinity',
                ['--family', 'locks', '--variant', variant, '--duration-ms', 20 if quick else 100,
                 '--cpus', ','.join(map(str, physical[:2] if variant == 'owner' else physical[1:2]))])
            add(f'confirm-held-lock-n{n}-{variant}', 'loom_affinity',
                ['--family', 'locks', '--variant', variant, '--duration-ms', 20 if quick else 100,
                 '--holder-pause-us', 1000,
                 '--cpus', cpus if variant == 'owner' else ','.join(map(str, physical[1:n]))])
        for duplicates in [1, 4, 16]:
            for variant in ['owner', 'shared', 'replicate']:
                add(f'confirm-output-n{n}-{variant}-d{duplicates}', 'loom_affinity',
                    ['--family', 'join', '--variant', variant, '--cpus', cpus,
                     '--build-rows', 4096 if quick else 524288,
                     '--probe-rows', 2048 if quick else 262144,
                     '--duplicates', duplicates, '--output-cap', 64,
                     '--distribution', 'uniform'])
    return cases
