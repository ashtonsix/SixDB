#!/usr/bin/env python3
"""Generate an auditable research header overlay; never edits the maintained owner."""
import argparse,difflib,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--records',type=Path,required=True);p.add_argument('--mode',choices=['callback','result','dummy','select','lean','carry'],default='callback');a=p.parse_args()
source=Path('ikea/include/ikea/hashtable');dest=a.output/'ikea/hashtable'
shutil.copytree(source,dest,dirs_exist_ok=True)
p=dest/'table.h';before=p.read_text()
needle='    [[gnu::always_inline]] bool finish(const probe& p, word key, Consume&& consume) const {\n'
assert before.count(needle)==1
addition='''#if defined(__AVX512VL__) && defined(__AVX512F__)
        if constexpr (!bitmap && Kernel == search_kernel::generic && (V == 0 || V == 8)) {
            const word matches = primary_matches(p.primary, p.hash.quotient);
            const bool found = matches != 0;
            // Combine the rare slow condition before branching. The opaque
            // register prevents LLVM from splitting it back into a hit branch.
            unsigned slow = unsigned(p.primary[overflow_offset]) & -unsigned(!found);
            asm("" : "+r"(slow));
            const unsigned slot = std::countr_zero(matches | (word{1} << N));
            value_type value{};
            if constexpr (V == 8) {
                // On absence slot==N is the one-past value row, but the zero
                // lane mask issues no value access. Only lane zero is enabled.
                auto selected = _mm_maskz_loadu_epi64(__mmask8(found),
                                                     p.primary + value_offset + slot * 8);
                asm("" : "+v"(selected));
                store<8>(value.data(), _mm_cvtsi128_si64(selected));
            }
            if (slow) [[unlikely]] {
                const auto result = read_overflow(key, p.hash.swiss);
                if constexpr (V)
                    if (overflow_found(result))
                        consume_located(consume, result.value, {p.hash.bucket, N, false});
                return overflow_found(result);
            }
            if constexpr (V)
                if (__builtin_unpredictable(found))
                    consume_located(consume, value, {p.hash.bucket, slot, true});
            return found;
        }
#endif
'''
if a.mode != 'callback':
 addition = '''    struct lookup_value { value_type value{}; bool found = false; };
    // Research value-returning read: absent values are exactly zero. This is
    // a separate contract; arbitrary callbacks still run only on actual hits.
    [[gnu::always_inline]] lookup_value lookup(word key) const {
#if defined(__AVX512VL__) && defined(__AVX512F__)
        if constexpr (!bitmap && (Kernel == search_kernel::generic ||
                                  Kernel == search_kernel::exact_small512) && (V == 0 || V == 8)) {
            const auto p = prepare(key);
            const word matches = primary_matches(p.primary, p.hash.quotient);
            lookup_value out;
            out.found = matches != 0;
            unsigned slow = unsigned(p.primary[overflow_offset]) & -unsigned(!out.found);
            asm("" : "+r"(slow));
            if (slow) [[unlikely]] {
                const auto spill = read_overflow(key, p.hash.swiss);
                return {spill.value, spill.found};
            }
            if constexpr (V == 8) {
                const unsigned slot = std::countr_zero(matches | (word{1} << N));
                unsigned active = out.found;
                asm("" : "+r"(active));
                auto selected = _mm_maskz_loadu_epi64(__mmask8(active),
                                                     p.primary + value_offset + slot * 8);
                asm("" : "+v"(selected));
                store<8>(out.value.data(), _mm_cvtsi128_si64(selected));
            }
            return out;
        }
#endif
        lookup_value out;
        out.found = find(key, [&](const auto& value) { out.value = value; });
        return out;
    }
'''
 if a.mode in ['dummy','select','lean','carry']:
  first='                unsigned active = out.found;\n                asm("" : "+r"(active));\n                auto selected = _mm_maskz_loadu_epi64(__mmask8(active),\n                                                     p.primary + value_offset + slot * 8);\n                asm("" : "+v"(selected));\n                store<8>(out.value.data(), _mm_cvtsi128_si64(selected));'
  second='                static constexpr word zero = 0;\n                const void* address = out.found ? static_cast<const void*>(p.primary + value_offset + slot * 8) : static_cast<const void*>(&zero);\n                asm("" : "+r"(address));\n                store<8>(out.value.data(), load<8>(static_cast<const byte*>(address)));'
  assert addition.count(first)==1;addition=addition.replace(first,second)
 if a.mode in ['select','lean','carry']:
  before_pointer='''                const void* address = out.found ? static_cast<const void*>(p.primary + value_offset + slot * 8) : static_cast<const void*>(&zero);
                asm("" : "+r"(address));'''
  after_pointer='''                const void* address = &zero;
                const void* present = p.primary + value_offset + slot * 8;
                asm("testq %2, %2; cmovneq %1, %0" : "+r"(address)
                    : "r"(present), "r"(matches) : "cc");'''
  assert addition.count(before_pointer)==1
  addition=addition.replace(before_pointer,after_pointer)
 if a.mode in ['lean','carry']:
  start=addition.index('            const word matches =')
  end=addition.index('            if constexpr (V == 8)',start)
  addition=addition[:start]+'''            // Buckets with any overflow retain the ordinary callback read.
            // This branch depends only on bucket state, never query membership.
            unsigned overflow = p.primary[overflow_offset];
            asm("" : "+r"(overflow));
            if (overflow) [[unlikely]] {
                lookup_value out;
                out.found = find(key, [&](const auto& value) { out.value = value; });
                return out;
            }
            const word matches = primary_matches(p.primary, p.hash.quotient);
            lookup_value out;
            out.found = matches != 0;
'''+addition[end:]
 if a.mode == 'carry':
  start=addition.index('                const unsigned slot =')
  end=addition.index('            }\n            return out;',start)
  addition=addition[:start]+'''                static constexpr word zero = 0;
                word address;
                // TZCNT's carry bit says whether the mask was zero. LEA and
                // CMOV preserve it, so one flag also supplies result.found.
                asm("tzcntq %2, %0; leaq (%3,%0,8), %0; cmovcq %4, %0"
                    : "=&r"(address), "=@ccae"(out.found)
                    : "r"(matches), "r"(p.primary + value_offset), "r"(&zero));
                store<8>(out.value.data(), load<8>(reinterpret_cast<const byte*>(address)));
'''+addition[end:]
 needle='    template <bool Eager = false, class Consume>\n'
 assert before.count(needle)==1
 after=before.replace(needle,addition+needle)
else:
 after=before.replace(needle,needle+addition)
p.write_text(after)
a.records.mkdir(parents=True,exist_ok=True)
(a.records/(a.mode+'-candidate.diff')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='maintained/table.h',tofile='candidate/table.h')))
(a.records/(a.mode+'-overlay.json')).write_text(json.dumps(dict(before_sha256=hashlib.sha256(before.encode()).hexdigest(),after_sha256=hashlib.sha256(after.encode()).hexdigest()),indent=2)+'\n')
