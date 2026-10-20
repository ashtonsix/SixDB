#!/usr/bin/env python3
"""Generate checked research deltas, never a second maintained owner copy."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def replace(s, old, new, count=1):
    actual = s.count(old)
    if actual != count:
        raise RuntimeError(f"expected {count}, found {actual}: {old!r}")
    return s.replace(old, new)


def generate(source, output, variant):
    root = source / 'ikea/include/ikea/hashtable'
    out = output / 'ikea/hashtable'
    files = {p.relative_to(root): p.read_text() for p in root.rglob('*.h')}
    original = files[Path('table.h')]
    table = original
    if variant != 'baseline':
        p = Path('detail/control.h')
        files[p] = replace(files[p], 'control_empty = 0x80, control_tombstone = 0xfe',
                           'control_empty = 0, control_tombstone = 1')
        p = Path('detail/hash.h')
        h = replace(files[p], '    word seed;', '''    static byte tag(word h) {
        const byte x = byte(h >> 56);
        return byte(x + (x < 2 ? 8 : 0));
    }
    word seed;''')
        files[p] = h
        table = replace(table, 'old_control.data[i] < 128', 'old_control.data[i] >= 2')
        table = replace(table, 'control_.data[i] < 128', 'control_.data[i] >= 2', 2)
    if variant == 'filter15':
        # No speculative mutation of continuation bytes through the guarded API:
        # publication of these additional byte ranges is outside this spike.
        start = table.index('    template <class Context, class Consume>\n    lookup_status guarded_find')
        end = table.index('    template <class Visit> void visit', start)
        table = table[:start] + table[end:]
        table = replace(table, '    static constexpr std::size_t absent', '''    static byte continuation_bit(word h) {
        return byte(1u << ((h >> 56) & 7));
    }
    static std::size_t control_index(std::size_t slot) {
        return (slot / 15) * 16 + slot % 15;
    }
    void mark_continuation(word hash, std::size_t slot) {
        const auto destination = slot / 15;
        std::size_t group = hash & (groups_ - 1), step = 0;
        while (group != destination) {
            control_.data[group * 16 + 15] |= continuation_bit(hash);
            group = (group + (++step)) & (groups_ - 1);
        }
    }
    static constexpr std::size_t absent''')
        table = replace(table, 'if (equal_controls(controls, empty))',
                        'if (!(controls[15] & continuation_bit(h.swiss)) || step + 1 == groups_)')
        # Every SIMD tag/empty mask must exclude the continuation lane.
        table = re.sub(r'equal_controls\((controls?|control), ([^)]+)\)',
                       r'(equal_controls(\1, \2) & 0x7fffu)', table)
        for a, b in [('g * 16 + i', 'g * 15 + i'),
                     ('group * 16 + lane', 'group * 15 + lane'),
                     ('group * 16 + std::countr_zero', 'group * 15 + std::countr_zero'),
                     ('s.group * 16 + std::countr_zero', 's.group * 15 + std::countr_zero')]:
            table = table.replace(a, b)
        table = replace(table, 'next_slots(groups * 16 * tuple_bytes, memory_)',
                        'next_slots(groups * 15 * tuple_bytes, memory_)')
        table = replace(table, 'old_groups * 16', 'old_groups * 15')
        table = replace(table, 'old_control.data[i] >= 2',
                        'old_control.data[control_index(i)] >= 2')
        table = replace(table, 'groups_ * 16 * 7', 'groups_ * 15 * 7', 3)
        table = replace(table, 'groups_ * 16;', 'groups_ * 15;', 2)
        table = replace(table, 'control_.data[i] >= 2',
                        'control_.data[control_index(i)] >= 2', 2)
        # Physical control indices differ from compact tuple indices.
        table = re.sub(r'control_\.data\[([^\]]*slot)\]',
                       r'control_.data[control_index(\1)]', table)
        # Only unchecked insertion and prepared-absent search step over full
        # groups. Normal store marks the path after proving absence.
        begin = table.index('    void insert_swiss_unchecked')
        end = table.index('    void rehash_swiss', begin)
        body = table[begin:end].replace('            g = (g + (++step))',
                    '            controls[15] |= continuation_bit(h.swiss);\n            g = (g + (++step))')
        table = table[:begin] + body + table[end:]
        table = replace(table, '        control_.data[control_index(slot)] = h.fingerprint;',
                        '        mark_continuation(h.swiss, slot);\n        control_.data[control_index(slot)] = h.fingerprint;')
        table = replace(table, '                control_.data[control_index(slot)] = position.hash.fingerprint;',
                        '                mark_continuation(position.hash.swiss, slot);\n                control_.data[control_index(slot)] = position.hash.fingerprint;')
        start = table.index('    swiss_position resolve_swiss(')
        end = table.index('    void store_swiss(', start)
        table = table[:start] + '''    swiss_position resolve_swiss(word key, const typename mix_type::result& h) const {
        if (!groups_) return {absent, false};
        auto group = h.swiss & (groups_ - 1);
        std::size_t step = 0, available = absent;
        bool proven_absent = false;
        for (;;) {
            const auto* control = control_.data + group * 16;
            if (!proven_absent) {
                auto candidates = equal_controls(control, h.fingerprint) & 0x7fffu;
                while (candidates) {
                    const auto lane = std::countr_zero(candidates);
                    candidates &= candidates - 1;
                    const auto slot = group * 15 + lane;
                    if (tuple_key(slots_.data + slot * tuple_bytes) == key)
                        return {slot, true};
                }
                proven_absent = !(control[15] & continuation_bit(h.swiss));
            }
            const auto empties = equal_controls(control, empty) & 0x7fffu;
            if (available == absent && empties)
                available = group * 15 + std::countr_zero(empties);
            if (proven_absent && available != absent) return {available, false};
            if (step + 1 == groups_) return {available, false};
            group = (group + (++step)) & (groups_ - 1);
        }
    }
''' + table[end:]
        table = replace(table, '            s.empty_group = (equal_controls(controls, empty) & 0x7fffu) != 0;',
                        '            s.empty_group = !(controls[15] & continuation_bit(s.first.hash.swiss));')
        table = replace(table, '        if (s.empty_group) {',
                        '        if (s.empty_group || s.step + 1 == groups_) {')
        table = replace(table, 'control_.data[control_index(found.slot)] = tombstone;',
                        'control_.data[control_index(found.slot)] = empty;')
        table = replace(table, '                    ++deleted_;',
                        '                    deleted_ += bool(control_.data[(found.slot / 15) * 16 + 15] & continuation_bit(h.swiss));')
        # Erasure budget exhaustion cleans at the same size if live load fits.
        table = replace(table, 'rehash_swiss(groups_ ? groups_ * 2 : 1);',
                        'rehash_swiss(groups_ && (spill_size_ + 1) * 8 <= groups_ * 15 * 7 ? groups_ : (groups_ ? groups_ * 2 : 1));', 3)
        # prepare_write is an absence-proven slot search; mark at commit instead.
        # Empty metadata initialization also resets all continuation bytes to 0.
        table = replace(table, '                deleted_ -= controls[i] == tombstone;\n', '')
        table = replace(table, '\n        deleted_ -= control_.data[control_index(slot)] == tombstone;', '')
        table = replace(table, '\n                deleted_ -= control_.data[control_index(slot)] == tombstone;', '')
        table = replace(table, '(equal_controls(controls, empty) & 0x7fffu) | (equal_controls(controls, tombstone) & 0x7fffu)',
                        '(equal_controls(controls, empty) & 0x7fffu)', 2)
    if variant != 'baseline':
        # Keep the original hash result and all primary writer work intact.
        # Derive the wider tag only inside overflow operations/control stages.
        table = re.sub(r'\bh\.fingerprint\b', 'mix_type::tag(h.swiss)', table)
        table = replace(table, 'position.hash.fingerprint', 'mix_type::tag(position.hash.swiss)')
        table = replace(table, 's.first.hash.fingerprint', 'mix_type::tag(s.first.hash.swiss)')
        table = table.replace('* 8 > groups_ * 16 * 7', '* 32 > groups_ * 16 * OVERFLOW_SWISS_CEILING32')
        table = table.replace('* 8 > groups_ * 15 * 7', '* 32 > groups_ * 15 * OVERFLOW_SWISS_CEILING32')
        table = table.replace('* 8 <= groups_ * 15 * 7', '* 32 <= groups_ * 15 * OVERFLOW_SWISS_CEILING32')
        table = replace(table, '#pragma once', '#pragma once\n#ifndef OVERFLOW_SWISS_CEILING32\n#define OVERFLOW_SWISS_CEILING32 28\n#endif')
    files[Path('table.h')] = table
    for name, text in files.items():
        p = out / name
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text() != text:
            p.write_text(text)
    delta = json.dumps({
        'variant': variant,
        'baseline_table_sha256': hashlib.sha256(original.encode()).hexdigest(),
        'generated_table_sha256': hashlib.sha256(table.encode()).hexdigest(),
    }, indent=2) + '\n'
    p = output / 'delta.json'
    if not p.exists() or p.read_text() != delta:
        p.write_text(delta)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=['baseline', 'tag8', 'filter15'], required=True)
    a = p.parse_args()
    generate(a.source, a.output, a.variant)
