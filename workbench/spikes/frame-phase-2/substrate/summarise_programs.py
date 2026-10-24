# Fixed stored programs against the per-operation oracle. For each column,
# ISA and condition: per-op medians, then named mixes (operation counts per
# workload unit), the best single program per mix and the per-op oracle.
import json, sys, statistics as st, collections
MIXES = {  # ops per workload unit: full SUMs, COUNTs, selective pairs, point batches of 65,536
    'scan': {'sum': 6, 'count_ge': 3, 'pair_sum_10pct': 1, 'points': 0},
    'selective': {'sum': 1, 'count_ge': 2, 'pair_sum_10pct': 6, 'points': 1},
    'points': {'sum': 1, 'count_ge': 1, 'pair_sum_10pct': 1, 'points': 7},
    'mixed': {'sum': 3, 'count_ge': 3, 'pair_sum_10pct': 3, 'points': 3},
}
d = collections.defaultdict(list); meta = {}
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['condition'], r['program'], r['op'])].append(r['ns'])
        meta[(r['isa'], r['column'], r['program'])] = (r['bytes'], r['build_ns_per_block'])
keys = sorted({k[:3] for k in d})
print('isa column condition mix | best program (us per unit) | oracle (us) | gap | natural MB per program')
for k in keys:
    progs = sorted({p for (*kk, p, op) in d if tuple(kk) == k})
    cost = {p: {op: st.median(d[k + (p, op)]) / 1e3 for op in MIXES['scan']} for p in progs}
    for mix, w in MIXES.items():
        totals = {p: sum(w[op] * cost[p][op] for op in w) for p in progs}
        best = min(totals, key=totals.get)
        oracle = sum(w[op] * min(cost[p][op] for p in progs) for op in w)
        mb = ' '.join(f"{p}={meta[(k[0], k[1], p)][0] / 1e6:.1f}" for p in progs)
        print(f"{k[0]:6} {k[1]:18} {k[2]:7} {mix:9} | {best:11} {totals[best]:9.1f} | {oracle:9.1f} | {totals[best] / oracle:5.2f} | {mb}")
