#!/usr/bin/env python3
"""Research compile-time predictability hint; existing find/write APIs stay intact."""
import argparse,difflib,hashlib,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--records',type=Path,required=True);a=p.parse_args()
subprocess.run([sys.executable,str(Path(__file__).with_name('overlay.py')),str(a.output),'--records',str(a.records),'--mode','carry'],check=True)
p=a.output/'ikea/hashtable/table.h';before=Path('ikea/include/ikea/hashtable/table.h').read_text();s=p.read_text()
needle='namespace ikea::hashtable {\n';assert s.count(needle)==1
s=s.replace(needle,needle+'// Performance hint only: neither enumerator promises presence or absence.\nenum class lookup_hint { predictable, unpredictable };\n',1)
needle='    template <bool Eager = false, class Consume>\n';assert s.count(needle)==1
addition='''    template <lookup_hint Hint = lookup_hint::predictable>
    [[gnu::always_inline]] lookup_value get(word key) const {
        if constexpr (Hint == lookup_hint::unpredictable)
            return lookup(key);
        else {
            lookup_value out;
            out.found = find(key, [&](const auto& value) { out.value = value; });
            return out;
        }
    }
'''
s=s.replace(needle,addition+needle);p.write_text(s);a.records.mkdir(parents=True,exist_ok=True)
(a.records/'hint-candidate.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='maintained/table.h',tofile='hint/table.h')))
(a.records/'hint-overlay.json').write_text(json.dumps(dict(before_sha256=hashlib.sha256(before.encode()).hexdigest(),after_sha256=hashlib.sha256(s.encode()).hexdigest()),indent=2)+'\n')
