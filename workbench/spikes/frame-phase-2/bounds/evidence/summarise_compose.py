import json,collections,statistics,sys
d=collections.defaultdict(list)
for f in sys.argv[1:]:
  for l in open(f):
    r=json.loads(l); d[(r['isa'],r['column'],r['method'],r.get('exception_penalty',0),r.get('max_exceptions',256))].append(r)
print(f"{'':24}{'arm':14}{'bits/val':>9}{'enc ns':>9}{'SUM ns':>8}{'CNT ns':>8}{'point':>7}{'exc/blk':>8}")
for k,v in d.items():
    m=lambda f: statistics.median(x[f] for x in v)
    print(f"{k[0]+' '+k[1]:24}{k[2]+' p'+str(k[3])+' c'+str(k[4]):14}{v[0]['bits_per_value']:9.3f}{m('encode_ns_per_block'):9.0f}{m('sum_ns_per_block'):8.1f}{m('count_ns_per_block'):8.1f}{m('point_ns'):7.1f}{(v[0]['low_exceptions']+v[0]['high_exceptions'])/11718:8.1f}")
