#!/usr/bin/env python3
"""Generate static hinted getters or measured, caller-owned block selection."""
import argparse,difflib,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--original',action='store_true');a=p.parse_args()
source=Path('workbench/spikes/hashtable-boost-adoption/memory-bench.cpp');before=source.read_text()
start=before.index('template <class T>\nResult read_requests(');end=before.index('// Order controls',start)
kernel=Path(__file__).with_name('hint-kernel.inc').read_text()
if a.original:
 kernel='struct SelectionStats { word predictable=0, unpredictable=0, probes=0, switches=0; };\nSelectionStats last_selection;\n'+before[start:end]
s=before[:start]+kernel+'\n'+before[end:]
s=s.replace('#include "../../benchmarks/hashtable/common.h"','#include "'+str(Path('workbench/benchmarks/hashtable/common.h').resolve())+'"')
# New orderings preserve the original multiset and within-hit/miss order.
needle='    queries.clear();\n    const auto run ='
addition='''    if (order >= 4) {
        queries.clear();
        const std::size_t length = order == 4 ? 131071 : (order == 5 ? 16381 : 509);
        std::size_t h=0, m=0;
        while(h<hits.size() || m<misses.size()) {
            const auto at=queries.size();
            const auto kind=((at+7759)/length)%4;
            bool hit=kind==0 || (kind==1 && (screen::random_bits(at+991)&1)) ||
                     (kind==3 && (at&1));
            if(h==hits.size()) hit=false;
            if(m==misses.size()) hit=true;
            queries.push_back(hit?hits[h++]:misses[m++]);
        }
        return;
    }
    queries.clear();
    const auto run ='''
assert s.count(needle)==1;s=s.replace(needle,addition).replace('ht::require(value <= 3, "query order")','ht::require(value <= 6, "query order")')
needle="<< peak_allocated + sizeof(T) << ',' << o.order << ',' << o.hits_only << '\\n';"
replacement="<< peak_allocated + sizeof(T) << ',' << o.order << ',' << o.hits_only << ',' << last_selection.predictable << ',' << last_selection.unpredictable << ',' << last_selection.probes << ',' << last_selection.switches << ',' << current_query_hash << '\\n';"
assert s.count(needle)==1;s=s.replace(needle,replacement)
s=s.replace('query_order,hits_only\\n','query_order,hits_only,method_predictable,method_unpredictable,selector_probes,selector_switches,query_hash\\n')
s=s.replace('using Clock = std::chrono::steady_clock;','using Clock = std::chrono::steady_clock;\nword current_query_hash = 0;')
needle='                reorder_queries(queries, hits, misses, o.order);'
assert s.count(needle)==1
s=s.replace(needle,needle+'''
                current_query_hash = 0x8719a23b49ab83e1ULL;
                for (word key : queries)
                    current_query_hash = (current_query_hash ^ key) * 0x9e3779b185ebca87ULL;''')
assert s.rstrip().endswith('#endif\n}')
s=s.rstrip()[:-1]+'    return 0;\n}\n'
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(s)
a.output.with_suffix('.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='ordinary/memory-bench.cpp',tofile='hint/memory-bench.cpp')))
a.output.with_suffix('.json').write_text(json.dumps(dict(before_sha256=hashlib.sha256(before.encode()).hexdigest(),after_sha256=hashlib.sha256(s.encode()).hexdigest()),indent=2)+'\n')
