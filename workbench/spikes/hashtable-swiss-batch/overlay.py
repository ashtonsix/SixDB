#!/usr/bin/env python3
"""Private reader/resolver deltas; no layout, insertion or occupancy change."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def body(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return start, end, text[opening + 1:end - 1]


def generate(source, output, variant, read_only=False):
    root = source / 'ikea/include/ikea/hashtable'
    files = {p.relative_to(root): p.read_text() for p in root.rglob('*.h')}
    originals = files.copy()
    if variant != 'group16':
        groups = 2 if variant == 'batch32' else 4
        line_bytes = {'line32': 32, 'line64': 64}.get(variant)
        tail = variant == 'tail64'
        path = Path('table.h')
        text = files[path]
        text = text.replace('#include "detail/control.h"', '#include "detail/control.h"\n#include "detail/batch.h"')
        for resolve in ([False] if read_only else [False, True]):
            signature = ('    swiss_position resolve_swiss(' if resolve else '    std::size_t find_swiss(')
            start, end, original = body(text, signature)
            candidates = '''
            auto candidates = packet.candidates;
            while (candidates) {
                const auto lane = std::countr_zero(candidates);
                candidates &= candidates - 1;
                const auto slot = packet.slot(lane);
                if (tuple_key(slots_.data + slot * tuple_bytes) == key)
                    RETURN_HIT;
            }
'''.replace('RETURN_HIT', 'return {slot, true}' if resolve else 'return slot')
            if resolve:
                candidates += '''
            if (available == absent && packet.vacancies)
                available = packet.slot(std::countr_zero(packet.vacancies));
            if (packet.empties)
                return {available, false};
'''
            else:
                candidates += '''
            if (packet.empties)
                return absent;
'''
            # Reuse the exact existing scalar body for the small-table fallback
            # and, optionally, the first group. Batched tails preserve its order.
            scalar_start = original.index('        for (;;) {')
            setup = original[:scalar_start]
            loop = original[scalar_start:]
            # Both original loops name the current group differently.
            if not resolve:
                setup = setup.replace('std::size_t g =', 'std::size_t group =')
                loop = loop.replace('g * 16', 'group * 16').replace('g = (g +', 'group = (group +')
            gate = ''
            if tail:
                gate = loop.replace('        for (;;) {', '        {', 1)
            fallback = '        if (groups_ < ' + str(groups) + ') {\n' + loop + '\n        }\n'
            batch_loop = f'''        for (;;) {{
            const auto packet = batch::scan<{groups}, {'true' if resolve else 'false'}>(
                control_.data, groups_, group, step, h.fingerprint());
{candidates}
            group = packet.next_group;
            step = packet.next_step;
        }}
'''
            if line_bytes:
                first = f'''        {{
            const auto packet = batch::first_line<{line_bytes}, {'true' if resolve else 'false'}>(
                control_.data, groups_, group, h.fingerprint());
{candidates}
            group = packet.next_group;
            step = packet.next_step;
        }}
'''
                replacement_body = setup + fallback + first + loop
            else:
                replacement_body = setup + gate + fallback + batch_loop
            replacement = text[start:text.index('{', start) + 1] + replacement_body + '    }'
            if read_only:
                replacement = text[start:end] + '\n' + replacement.replace('find_swiss(', 'find_swiss_read(', 1)
            text = text[:start] + replacement + text[end:]
        if read_only:
            start, end, _ = body(text, ' overflow_result read_overflow(')
            reader = text[start:end].replace('find_swiss(key, h)', 'find_swiss_read(key, h)')
            text = text[:start] + reader + text[end:]
        files[path] = text
        files[Path('detail/batch.h')] = Path(__file__).with_name('batch.h').read_text()
    # Check builds can compare the actual reader's full-key accesses against
    # an independent scalar walk. Preprocessing removes all of this in release.
    path = Path('table.h')
    text = files[path]
    text = text.replace('#include "detail/control.h"', '#include "detail/control.h"\n#ifdef SIXDB_SWISS_TRACE\n#include "detail/batch.h"\n#endif')
    for signature in ['    std::size_t find_swiss(', '    std::size_t find_swiss_read(']:
        if signature not in text:
            continue
        start, end, _ = body(text, signature)
        reader = re.sub(r'(\s+)(if \(tuple_key\(slots_\.data \+ (\w+) \* tuple_bytes\) == key\))',
                        lambda m: m[1] + '#ifdef SIXDB_SWISS_TRACE\n                batch::record(' + m[3] + ');\n#endif' + m[1] + m[2], text[start:end])
        text = text[:start] + reader + text[end:]
    text = text.replace('    [[gnu::noinline, gnu::pure]] overflow_result read_overflow',
                        '#ifdef SIXDB_SWISS_TRACE\n    [[gnu::noinline]]\n#else\n    [[gnu::noinline, gnu::pure]]\n#endif\n    overflow_result read_overflow')
    files[path] = text
    files[Path('detail/batch.h')] = Path(__file__).with_name('batch.h').read_text()
    changes = [{'path': str(path),
                'before': hashlib.sha256(originals[path].encode()).hexdigest() if path in originals else None,
                'after': hashlib.sha256(text.encode()).hexdigest()}
               for path, text in files.items() if text != originals.get(path)]
    for path, text in files.items():
        p = output / 'ikea/hashtable' / path
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text() != text:
            p.write_text(text)
    (output / 'delta.json').write_text(json.dumps({'variant': variant, 'read_only': read_only, 'changes': changes,
        'trace_annotations': 'preprocessed away unless SIXDB_SWISS_TRACE is defined'}, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=['group16', 'batch32', 'batch64', 'tail64', 'line32', 'line64'], required=True)
    p.add_argument('--read-only', action='store_true')
    a = p.parse_args()
    generate(a.source, a.output, a.variant, a.read_only)
