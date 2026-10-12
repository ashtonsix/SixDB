#!/usr/bin/env python3
"""Executable placement/accounting model; no physical storage or timing claims."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import random
from model import Permutation, split, reconstruct


class Table:
    def __init__(self, bits, a, secondary_a, slots, seed):
        self.bits, self.a, self.secondary_a, self.slots = bits, a, secondary_a, slots
        self.p, self.q = Permutation(bits, seed), Permutation(bits, seed ^ 0x736563)
        # Sparse model directories allow the a=w boundary without allocating 2^w.
        # Every record retains only fingerprint, suffix and value; owner bucket
        # bits are supplied by its container, including the chain's secondary owner.
        self.primary, self.secondary, self.chain = [collections.defaultdict(list) for _ in range(3)]
        self.descendants, self.chain_count = [collections.Counter() for _ in range(2)]

    def parts(self, key):
        return (split(self.p.forward(key), self.bits, self.a),
                split(self.q.forward(key), self.bits, self.secondary_a))

    @staticmethod
    def search(entries, fp, suffix):
        return next((i for i, e in enumerate(entries) if e[:2] == (fp, suffix)), None)

    def locate(self, key):
        (first, fp, suffix), (second, sfp, ssuffix) = self.parts(key)
        i = self.search(self.primary.get(first, ()), fp, suffix)
        if i is not None:
            return self.primary[first], i, 0, first, second
        if self.descendants[first]:
            i = self.search(self.secondary.get(second, ()), sfp, ssuffix)
            if i is not None:
                return self.secondary[second], i, 1, first, second
            if self.chain_count[second]:
                i = self.search(self.chain.get(second, ()), sfp, ssuffix)
                if i is not None:
                    return self.chain[second], i, 2, first, second
        return None

    def get(self, key):
        found = self.locate(key)
        return None if found is None else found[0][found[1]][2]

    def set(self, key, value):
        # Search descendants before consuming a newly available primary vacancy.
        found = self.locate(key)
        if found is not None:
            entries, i = found[:2]
            entries[i] = (*entries[i][:2], value)
            return 0
        (first, fp, suffix), (second, sfp, ssuffix) = self.parts(key)
        if len(self.primary[first]) < self.slots:
            self.primary[first].append((fp, suffix, value))
        else:
            self.descendants[first] += 1
            if len(self.secondary[second]) < self.slots:
                self.secondary[second].append((sfp, ssuffix, value))
            else:
                self.chain_count[second] += 1
                self.chain[second].append((sfp, ssuffix, value))
        return 1

    def erase(self, key):
        found = self.locate(key)
        if found is None:
            return 0
        entries, i, tier, first, second = found
        entries[i] = entries[-1]
        entries.pop()
        if tier:
            self.descendants[first] -= 1
        if tier == 2:
            self.chain_count[second] -= 1
        return 1

    def items(self):
        for tier, (directory, permutation, a) in enumerate((
                (self.primary, self.p, self.a), (self.secondary, self.q, self.secondary_a),
                (self.chain, self.q, self.secondary_a))):
            for owner, entries in directory.items():
                for fp, suffix, value in entries:
                    yield permutation.inverse(reconstruct((owner, fp, suffix), self.bits, a)), value, tier, owner

    def audit(self, reference):
        items = list(self.items())
        assert len(items) == len(reference)  # dict conversion alone hides duplicates.
        assert {key: value for key, value, _, _ in items} == reference
        descendants, chain_count = collections.Counter(), collections.Counter()
        for key, value, tier, owner in items:
            assert self.get(key) == value
            if tier:
                descendants[self.p.forward(key) >> (self.bits - self.a)] += 1
            if tier == 2:
                chain_count[owner] += 1
        assert +self.descendants == descendants and +self.chain_count == chain_count
        assert all(n >= 0 for n in self.descendants.values())
        assert all(n >= 0 for n in self.chain_count.values())
        assert all(len(e) <= self.slots for d in (self.primary, self.secondary) for e in d.values())


def run():
    counts = collections.Counter()
    for bits in range(8, 65, 8):
        for a in (0, 1, 3, bits):
            for slots in (1, 4):
                rng = random.Random(bits * 997 + a * 11 + slots)
                keys = list(dict.fromkeys([0, (1 << bits)-1] + [rng.getrandbits(bits) for _ in range(128)]))
                seed = bits * 1009 + a
                t = Table(bits, a, max(0, a-2), slots, seed)
                ref = {}
                for step in range(600):
                    key = rng.choice(keys)
                    if step % 151 == 150:
                        # Reconstruct the stored representation, reseed and vary
                        # growth level. The reference dictionary never supplies
                        # keys to the rebuild, so a lossy quotient is observable.
                        replacement = Table(bits, min(bits, a + step % 3), max(0, a-1), slots, seed + step)
                        for k, v, _, _ in t.items():
                            assert replacement.set(k, v) == 1
                        t = replacement
                        counts['rebuilds'] += 1
                    elif step % 3:
                        value = rng.getrandbits(64)
                        assert t.set(key, value) == (key not in ref)
                        ref[key] = value
                        counts['sets'] += 1
                    else:
                        found = t.locate(key)
                        if found is not None and found[1] + 1 < len(found[0]):
                            counts['non_last_erase_tier_' + str(found[2])] += 1
                        assert t.erase(key) == (key in ref)
                        ref.pop(key, None)
                        counts['erase_tier_' + str(None if found is None else found[2])] += 1
                    assert t.get(key) == ref.get(key)
                    t.audit(ref)
                    counts['audits'] += 1
                # All tiers must drain, with both descendant counters returning
                # to zero despite gaps and swap deletions. No promotion needed.
                for key in keys:
                    assert t.erase(key) == (key in ref)
                    ref.pop(key, None)
                    t.audit(ref)
                assert not any(t.descendants.values()) and not any(t.chain_count.values())
                counts['cases'] += 1

    # Deliberate primary vacancy plus update of a still-displaced key. Every key
    # shares the sole primary/secondary bucket; this must not insert a duplicate.
    t = Table(8, 0, 0, 1, 731)
    ref = {0: 10, 1: 11, 2: 12}
    for k, v in ref.items():
        assert t.set(k, v) == 1
    assert t.erase(0) == 1
    del ref[0]
    assert t.set(2, 99) == 0
    ref[2] = 99
    assert t.set(1, 101) == 0
    ref[1] = 101
    t.audit(ref)
    counts['explicit_vacancy_update'] = 1
    return counts


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    result = {'scope': 'logical set/map quotient reconstruction, no-promotion deletion and descendant accounting; no allocation, chain-link, multimap, concurrency or performance claim',
              'checks': run(),
              'sources': {f: hashlib.sha256(Path(__file__).with_name(f).read_bytes()).hexdigest()
                          for f in ('state_model.py', 'model.py')}}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
