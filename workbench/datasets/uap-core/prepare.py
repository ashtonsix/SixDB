"""Preserve source records, rule order, flags, fixture origins, and license."""
import json
import tarfile

DEFAULTS = {}
REVISION = '73e7340c3ed8055051607b296bf46ead7aa5f19e'


def prepare(sources, output):
    import yaml
    with tarfile.open(sources['uap']) as archive:
        def read(name):
            return archive.extractfile(f'uap-core-{REVISION}/' + name).read()
        rules = yaml.safe_load(read('regexes.yaml'))
        with (output / 'rules.jsonl').open('w', encoding='utf-8', newline='\n') as f:
            for group, entries in rules.items():
                for i, row in enumerate(entries):
                    f.write(json.dumps({'id': f'{group}:{i}', 'group': group, 'row': i, 'record': row}) + '\n')
        values = []
        with (output / 'fixtures.jsonl').open('w', encoding='utf-8', newline='\n') as f:
            for name in ('test_ua.yaml', 'test_os.yaml', 'test_device.yaml'):
                for i, row in enumerate(yaml.safe_load(read('tests/' + name))['test_cases']):
                    values.append(row['user_agent_string'])
                    f.write(json.dumps({'source': name, 'row': i, 'record': row}) + '\n')
        (output / 'LICENSE').write_bytes(read('LICENSE'))
    return {'strings': len(values), 'unique_strings': len(set(values)),
            'pattern_groups': {k: len(v) for k, v in rules.items()},
            'input_order': 'test_ua, test_os, test_device; source order, duplicates retained'}
