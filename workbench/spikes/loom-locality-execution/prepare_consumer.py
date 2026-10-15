#!/usr/bin/env python3
"""Derive only the memory-window emitter from the captured Ikea consumer."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--unique", action="store_true", help="derive unique physical-line class control and retain window inventory")
args = parser.parse_args()
text = args.source.read_text()
before = '''template<bool Hint> auto emitter(std::size_t page,std::size_t line) {return [=](const byte* p,std::size_t n) __attribute__((always_inline)) {
    if constexpr(Hint)if(n){const auto start=reinterpret_cast<std::uintptr_t>(p),end=start+n;
        loom::prefetch<loom::Hint::l1_1>(p,page,line);for(auto at=(start&~std::uintptr_t(line-1))+line;at<end;at+=line)loom::prefetch<loom::Hint::l1_1>(reinterpret_cast<const byte*>(at),page,line);}
};}'''
after = '''// Spike-only shapes are prepared once before timing; emitted windows remain
// the source reader's actual qualified spans. No binding or clock in a visit.
inline constexpr unsigned window_limit=1024;
struct window_shapes {
    std::array<loom_locality::ContiguousPlan,window_limit+1> shapes;
    explicit window_shapes(std::size_t line) {
        for(unsigned n=1;n<=window_limit;++n) {
            auto plan=loom_locality::bind_contiguous(n,n,line);
            require(bool(plan),"contiguous window shape");shapes[n]=*plan;
        }
    }
};
// One pinned owner in this finite mechanism control. The prepared table is
// immutable throughout each measurement; not a proposed global runtime cache.
inline const window_shapes* current_windows=nullptr;
template<bool Hint> auto emitter(std::size_t,std::size_t) {
    const auto* windows=current_windows;
    return [=](const byte* p,std::size_t n) __attribute__((always_inline)) {
        if constexpr(Hint) {
            __builtin_assume(n<=window_limit);
            loom_locality::prefetch<LOOM_WINDOW_LOCALITY>(windows->shapes[n],p);
        }
    };
}'''
if text.count(before) != 1:
    raise SystemExit("Captured Ikea emitter changed; review before deriving it")
result = '#include "contiguous.h"\n' + text.replace(before, after)
anchor = '    const auto generic_visits=inspect(*body,body->bind_read(),source);word word_visits=0;'
replacement = '''    window_shapes prepared_windows(worker.hints.profile.geometry.line_bytes);
    current_windows=&prepared_windows;
    // Independently enumerate actual emitted sizes before allowing indexed plans.
    unsigned largest_window=0;
    auto audit=[&](const auto& read) {
        for(unsigned i=0;i<source.requests;++i) {
            typename std::remove_cvref_t<decltype(read)>::probe p;bool found=false;
            auto window=[&](const byte*,std::size_t n) {require(n<=window_limit,"window exceeds prepared shapes");largest_window=std::max(largest_window,unsigned(n));};
            read.prepare_read(p,key(i),window);
            while(!read.advance_read(p,key(i),found,[](auto){},window)) {}
        }
    };
    audit(body->bind_read());
    if constexpr(WordControl)audit(word_reader<WordBody>{old->bind_read()});
    require(largest_window>0,"no actual windows inspected");
    const auto generic_visits=inspect(*body,body->bind_read(),source);word word_visits=0;'''
if result.count(anchor) != 1:
    raise SystemExit("Captured consumer qualification changed; review before deriving")
result = result.replace(anchor, replacement)
if args.unique:
    result = result.replace("loom_locality::prefetch<LOOM_WINDOW_LOCALITY>(windows->shapes[n],p);",
                            "loom_locality::prefetch_unique<LOOM_WINDOW_LOCALITY>(windows->shapes[n],p);")
    result = result.replace("unsigned largest_window=0;", "unsigned largest_window=0;std::array<std::uint64_t,window_limit+1> window_counts{};")
    result = result.replace("largest_window=std::max(largest_window,unsigned(n));", "largest_window=std::max(largest_window,unsigned(n));++window_counts[n];")
    result = result.replace('    require(largest_window>0,"no actual windows inspected");', '''    require(largest_window>0,"no actual windows inspected");
    std::fprintf(stderr,"window_inventory,line=%zu,cap=%zu,largest=%u",worker.hints.profile.geometry.line_bytes,
                 std::size_t(8)*worker.hints.profile.geometry.line_bytes,largest_window);
    for(unsigned n=0;n<=window_limit;++n)if(window_counts[n])
        std::fprintf(stderr,",%u:%llu",n,(unsigned long long)window_counts[n]);
    std::fputc('\\n',stderr);''')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(result)
receipt = {
    "source": str(args.source),
    "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
    "derived_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
    "changes": ["Include private contiguous header", "Replace window hint emitter only",
                "Prebind shapes and independently inventory emitted windows before timing"],
    "unchanged": ["all readers and storage", "complete-byte and request graph oracles",
                  "direct/manual/Ring loops", "native signature", "visit inventory",
                  "caller successor hints", "Abseil controls"],
}
if args.unique:
    receipt["changes"] += ["Select optional unique physical-line emitter", "Retain untimed window size/count inventory on stderr"]
args.output.with_suffix(".receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
