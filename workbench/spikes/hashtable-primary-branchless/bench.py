#!/usr/bin/env python3
"""Change only the timed read's consumption contract; retain all useful work."""
import argparse,difflib,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
p=Path('workbench/spikes/hashtable-boost-adoption/memory-bench.cpp');before=p.read_text()
start=before.index('        const bool hit = find(t, key,',before.index('[[gnu::noinline]] Result read_loop'))
end=before.index('\n    }\n    return result;',start)
addition='''        // All arms return the same zero-on-absence result and do identical
        // hit/key/value reductions. Vendor adapters retain native find.
        struct Match { Value value{}; bool found = false; };
        const auto match = [&] {
            if constexpr (requires { t.lookup(key); }) return t.lookup(key);
            else {
                Match out;
                out.found = find(t, key, [&](const Value& v) { out.value = v; });
                return out;
            }
        }();
        result.hits += match.found;
        if constexpr (Full) {
            result.keys += key & -word(match.found);
            for (unsigned j = 0; j < V; j += 8)
                result.values += ht::load<8>(match.value.data() + j);
        }'''
after=before[:start]+addition+before[end:]
# The generated TU lives outside the original source directory.
after=after.replace('#include "../../benchmarks/hashtable/common.h"','#include "'+str(Path('workbench/benchmarks/hashtable/common.h').resolve())+'"')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(after)
a.output.with_suffix('.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='callback/memory-bench.cpp',tofile='result/memory-bench.cpp')))
a.output.with_suffix('.json').write_text(json.dumps(dict(before_sha256=hashlib.sha256(before.encode()).hexdigest(),after_sha256=hashlib.sha256(after.encode()).hexdigest()),indent=2)+'\n')
