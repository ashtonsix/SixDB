#!/usr/bin/env python3
"""Extract fixed hint bodies and hot-path instruction evidence from objdump."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def functions(text):
    result = {}
    current = None
    for line in text.splitlines():
        match = re.match(r'^[0-9a-f]+ <(.+)>:$', line.strip())
        if match:
            current = match[1]
            result[current] = []
            continue
        instruction = re.match(r'^\s*[0-9a-f]+:\s*(.*)', line)
        if current is None or not instruction:
            continue
        rest = instruction[1]
        while (encoded := re.match(r'^(?:[0-9a-f]{8}|[0-9a-f]{2})\s+', rest)):
            rest = rest[encoded.end():]
        if rest and not rest.startswith(('.', '<')):
            result[current].append(rest.strip())
    return result


def counts(body):
    mnemonics = [line.split()[0] for line in body]
    return {
        'instructions': len(body),
        'prefetches': sum(m.startswith('prefetch') or m == 'prfm' for m in mnemonics),
        'conditional_branches': sum(bool(re.fullmatch(r'j(?!mp)[a-z]+|b\.[a-z]+|cbn?z|tbn?z', m))
                                    for m in mnemonics),
        'calls': [line for line in body if line.split()[0] in ['call', 'callq', 'bl', 'blr']],
        'divides': [line for line in body if re.fullmatch(r'[isu]?div[qwl]?', line.split()[0])],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = []
    for path in args.assembly:
        source = path.read_bytes()
        bodies = functions(source.decode())
        selected = {}
        for name, body in bodies.items():
            if re.fullmatch(r'loom_hint_\d+', name):
                selected[name] = counts(body) | {'body': body}
            elif ('callback<' in name and 'HintPath' in name) or '::hop<' in name or any(
                    token in name for token in ['::scalar_step(', '::vector_step(', '::produce(']) or any(
                    token in name for token in ['::yield(', 'Channels::receive(', 'Channels::route(']):
                selected[name] = counts(body)
        fixed = [f'loom_hint_{i}' for i in range(16)]
        if path.name == 'assembly-loom_microtask.stdout':
            expected = [0, 1, 1, 1, 2, 2, 4, 4, 8, 8, 3, 5, 3, 5, 9, 17]
            for name, count in zip(fixed, expected):
                record = selected[name]
                if record['prefetches'] != count or record['conditional_branches'] or record['calls']:
                    raise RuntimeError(f'fixed hint class did not lower as intended: {name}: {record}')
        result.append({'input': path.name, 'sha256': hashlib.sha256(source).hexdigest(),
                       'functions': selected})
    args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
