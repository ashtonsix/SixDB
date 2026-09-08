#include "factor.h"
#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <re2/regexp.h>

namespace lowering {
namespace {
Token gap() { return {Kind::many,{},infinity}; }
std::string encode_rune(int r, bool latin1) {
    if (latin1 || r<128) return std::string(1,char(r));
    std::string s;
    if (r<0x800) s+=char(0xc0|(r>>6));
    else if (r<0x10000) { s+=char(0xe0|(r>>12)); s+=char(0x80|((r>>6)&63)); }
    else { s+=char(0xf0|(r>>18)); s+=char(0x80|((r>>12)&63)); s+=char(0x80|((r>>6)&63)); }
    s+=char(0x80|(r&63)); return s;
}
void append(Glob& out, const Glob& part) {
    for (const auto& t:part) {
        if (!out.empty() && out.back().kind==t.kind) {
            if (t.kind==Kind::many) continue;
            if (t.kind==Kind::literal) { out.back().text+=t.text; out.back().maximum=out.back().text.size(); continue; }
        }
        out.push_back(t);
    }
}
struct Builder {
    Factored result;
    bool chains;
    size_t budget, visits=0;
    std::unordered_map<std::string,size_t> interned;
    std::unordered_map<re2::Regexp*,std::optional<Glob>> linear_cache;
    size_t intern(std::string key, Clause node) {
        if (auto it=interned.find(key); it!=interned.end()) return it->second;
        if (result.nodes.size()>=budget) { ++result.relaxations; return 1; }
        size_t id=result.nodes.size(); result.nodes.push_back(std::move(node));
        interned.emplace(std::move(key),id); return id;
    }
    size_t atom(Glob g) {
        Glob whole{gap()}; append(whole,g); append(whole,{gap()});
        if (whole.size()==1) return 1;
        std::string key="a";
        for (const auto& t:whole) key+=std::to_string(int(t.kind))+":"+std::to_string(t.text.size())+":"+t.text;
        return intern(std::move(key),{Bool::atom,{},std::move(whole)});
    }
    size_t combine(Bool op, std::vector<size_t> children) {
        if (op==Bool::all && result.ordered) op=Bool::sequence;
        const size_t identity=op==Bool::any ? 0 : 1, absorbing=1-identity;
        std::vector<size_t> unique;
        for (size_t child:children) {
            if (child==absorbing) return absorbing;
            if (child!=identity && (op==Bool::sequence || std::find(unique.begin(),unique.end(),child)==unique.end())) unique.push_back(child);
        }
        if (unique.empty()) return identity;
        if (unique.size()==1) return unique[0];
        auto canonical=unique;
        if (op!=Bool::sequence) std::sort(canonical.begin(),canonical.end());
        std::string key=op==Bool::sequence ? ">" : op==Bool::all ? "&" : "|";
        for (size_t id:canonical) key+=std::to_string(id)+",";
        return intern(std::move(key),{op,std::move(unique)});
    }
    std::optional<Glob> linear(re2::Regexp* r, size_t depth=0) {
        if (auto it=linear_cache.find(r); it!=linear_cache.end()) return it->second;
        auto g=linear_impl(r,depth);
        linear_cache.emplace(r,g); return g;
    }
    std::optional<Glob> linear_impl(re2::Regexp* r,size_t depth) {
        using namespace re2;
        if (++visits>20000 || depth>128) { ++result.relaxations; return Glob{gap()}; }
        auto child=[&](int i) { return linear(r->sub()[i],depth+1); };
        switch (r->op()) {
        case kRegexpNoMatch: case kRegexpAlternate: return std::nullopt;
        case kRegexpCapture: return child(0);
        case kRegexpLiteral: case kRegexpLiteralString: {
            if (r->parse_flags() & Regexp::FoldCase) return Glob{gap()};
            std::string s;
            int n=r->op()==kRegexpLiteral ? 1 : r->nrunes();
            for (int i=0;i<n;++i) s+=encode_rune(r->op()==kRegexpLiteral ? r->rune() : r->runes()[i],result.latin1);
            return Glob{{Kind::literal,s,s.size()}};
        }
        case kRegexpAnyChar: case kRegexpCharClass:
            return Glob{{Kind::one,{},result.latin1 ? size_t(1) : size_t(4)}};
        case kRegexpAnyByte:
            if (result.latin1) return Glob{{Kind::one,{},1}};
            return Glob{gap()};
        case kRegexpConcat: {
            Glob out;
            for (int i=0;i<r->nsub();++i) {
                auto part=child(i); if (!part) return std::nullopt;
                append(out,*part);
                if (out.size()>256) return std::nullopt; // Split into clauses instead.
            }
            return out;
        }
        case kRegexpStar: case kRegexpQuest: return Glob{gap()};
        case kRegexpPlus: case kRegexpRepeat: {
            int lo=r->op()==kRegexpPlus ? 1 : r->min();
            int hi=r->op()==kRegexpPlus ? -1 : r->max();
            if (!lo) return Glob{gap()};
            auto part=child(0); if (!part) return std::nullopt;
            Glob out;
            int copies=std::min(lo,16);
            for (int i=0;i<copies;++i) {
                append(out,*part);
                if (out.size()>256) { ++result.relaxations; return Glob{gap()}; }
            }
            if (hi!=lo || copies<lo) append(out,{gap()});
            return out;
        }
        case kRegexpEmptyMatch: case kRegexpBeginText: case kRegexpEndText:
        case kRegexpBeginLine: case kRegexpEndLine: case kRegexpWordBoundary:
        case kRegexpNoWordBoundary: return Glob{};
        default: ++result.relaxations; return Glob{gap()};
        }
    }
    size_t build(re2::Regexp* r,size_t depth=0) {
        using namespace re2;
        if (++visits>20000 || depth>128) { ++result.relaxations; return 1; }
        if (chains) if (auto g=linear(r,depth)) return atom(std::move(*g));
        auto child=[&](int i) { return build(r->sub()[i],depth+1); };
        switch (r->op()) {
        case kRegexpNoMatch: return 0;
        case kRegexpCapture: return child(0);
        case kRegexpLiteral: case kRegexpLiteralString: return atom(*linear(r,depth));
        case kRegexpAlternate: {
            std::vector<size_t> children;
            for (int i=0;i<r->nsub();++i) children.push_back(child(i));
            return combine(Bool::any,std::move(children));
        }
        case kRegexpConcat: {
            std::vector<size_t> children; Glob chunk;
            auto flush=[&] { if (!chunk.empty()) { children.push_back(atom(std::move(chunk))); chunk.clear(); } };
            for (int i=0;i<r->nsub();++i) {
                if (chains) if (auto part=linear(r->sub()[i],depth+1)) {
                    if (chunk.size()+part->size()>256) flush();
                    append(chunk,*part); continue;
                }
                flush(); children.push_back(child(i));
            }
            flush(); return combine(Bool::all,std::move(children));
        }
        case kRegexpPlus: return child(0);
        case kRegexpRepeat:
            if (r->min()<=0) return 1;
            if (result.ordered) return combine(Bool::all,std::vector<size_t>(size_t(std::min(r->min(),16)),child(0)));
            return child(0);
        default: return 1; // Optional terms, classes, wildcards and assertions.
        }
    }
};
} // namespace

Factored factor(const Plan& p,bool chains,size_t node_budget,bool ordered) {
    if (!p.oracle || !p.oracle->ok() || node_budget<2) throw std::invalid_argument("valid oracle and node budget >=2 required");
    Builder b{{},chains,node_budget,0,{}, {}}; b.result.latin1=p.latin1; b.result.ordered=ordered;
    b.result.root=b.build(p.oracle->Regexp()); return std::move(b.result);
}
std::vector<size_t> Factored::reachable() const {
    std::vector<bool> seen(nodes.size());
    std::function<void(size_t)> visit=[&](size_t id) {
        if (seen[id]) return; seen[id]=true;
        for (size_t child:nodes[id].children) visit(child);
    };
    visit(root); std::vector<size_t> out;
    for (size_t i=0;i<seen.size();++i) if (seen[i]) out.push_back(i);
    return out;
}
size_t Factored::atoms() const {
    size_t n=0; for (size_t id:reachable()) n+=nodes[id].op==Bool::atom; return n;
}
Work& Work::operator+=(const Work& x) {
    atoms+=x.atoms; offered_bytes+=x.offered_bytes; node_visits+=x.node_visits; cache_hits+=x.cache_hits; return *this;
}
bool evaluate(const Factored& f,std::string_view s,Work& work,const Order* order,
              bool eager,std::vector<uint64_t>* positives,bool independent) {
    if (f.ordered) throw std::invalid_argument("ordered DAG requires positional evaluation");
    std::vector<int8_t> memo(f.nodes.size(),-1);
    std::function<bool(size_t)> eval=[&](size_t id) {
        ++work.node_visits;
        if (memo[id]>=0) { ++work.cache_hits; return bool(memo[id]); }
        const auto& n=f.nodes[id]; bool value=n.op==Bool::yes || n.op==Bool::all;
        if (n.op==Bool::atom) {
            ++work.atoms; work.offered_bytes+=s.size();
            value=independent ? like_oracle(n.atom,s,f.latin1) : like(n.atom,s,f.latin1);
        } else if (n.op==Bool::all || n.op==Bool::any) {
            for (size_t child:order ? (*order)[id] : n.children) {
                bool v=eval(child);
                if ((n.op==Bool::all && !v) || (n.op==Bool::any && v)) { value=v; break; }
            }
        }
        memo[id]=int8_t(value); return value;
    };
    if (eager) for (size_t id:f.reachable()) eval(id);
    bool result=eval(f.root);
    if (positives) {
        if (!eager || positives->size()!=f.nodes.size()) throw std::invalid_argument("eager observation vector required");
        for (size_t id:f.reachable()) (*positives)[id]+=memo[id]==1;
    }
    return result;
}
Order train_order(const Factored& f,const std::vector<uint64_t>& positives,size_t rows) {
    if (f.ordered || !rows || positives.size()!=f.nodes.size()) throw std::invalid_argument("unordered DAG and training counts required");
    Order order(f.nodes.size());
    std::vector<size_t> costs(f.nodes.size());
    for (size_t i=0;i<f.nodes.size();++i) {
        Factored sub=f; sub.root=i; costs[i]=std::max(size_t(1),sub.atoms());
        order[i]=f.nodes[i].children;
        auto score=[&](size_t child) {
            double p=double(positives[child])/double(rows);
            return (f.nodes[i].op==Bool::all ? 1-p : p)/double(costs[child]);
        };
        std::stable_sort(order[i].begin(),order[i].end(),[&](size_t a,size_t b) { return score(a)>score(b); });
    }
    return order;
}
bool ordered_match(const Factored& f,std::string_view s,PositionWork& work,bool independent) {
    if (!f.ordered) throw std::invalid_argument("ordered DAG required");
    auto next=[&](size_t p) {
        if (p>=s.size()) return s.size()+1;
        ++p;
        if (!f.latin1) while (p<s.size() && (static_cast<unsigned char>(s[p])&0xc0)==0x80) ++p;
        return p;
    };
    const size_t missing=s.size()+1;
    // Memoization keys include the incoming position. Reusing a Boolean result
    // across positions would incorrectly allow the same occurrence twice.
    std::vector<std::unordered_map<size_t,size_t>> memo(f.nodes.size());
    size_t entries=0;
    auto end_of_atom=[&](const Glob& g,size_t start) {
        ++work.atoms;
        if (independent) {
            // Prefix enumeration with the independent whole-value LIKE oracle.
            for (size_t end=start;end<=s.size();end=next(end))
                if (like_oracle(g,s.substr(start,end-start),f.latin1)) return end;
            return missing;
        }
        std::vector<uint8_t> current(s.size()+1),following(s.size()+1); current[start]=1;
        work.peak_buffer_bytes=std::max(work.peak_buffer_bytes,2*(s.size()+1));
        // The final search % need not consume anything at earliest completion.
        for (size_t i=0;i+1<g.size();++i) {
            const auto& t=g[i]; std::fill(following.begin(),following.end(),0);
            for (size_t pos=start;pos<=s.size();pos=next(pos)) {
                ++work.position_visits;
                if (t.kind==Kind::many) {
                    if (current[pos]) following[pos]=1;
                    if (following[pos] && pos<s.size()) following[next(pos)]=1;
                } else if (current[pos]) {
                    if (t.kind==Kind::one && pos<s.size()) following[next(pos)]=1;
                    else if (t.kind==Kind::literal && s.substr(pos).starts_with(t.text)) following[pos+t.text.size()]=1;
                }
            }
            current.swap(following);
        }
        for (size_t end=start;end<=s.size();end=next(end)) if (current[end]) return end;
        return missing;
    };
    std::function<size_t(size_t,size_t)> visit=[&](size_t id,size_t start) {
        ++work.node_visits;
        if (auto it=memo[id].find(start); it!=memo[id].end()) { ++work.cache_hits; return it->second; }
        const auto& node=f.nodes[id]; size_t end=missing;
        if (node.op==Bool::yes) end=start;
        else if (node.op==Bool::atom) end=end_of_atom(node.atom,start);
        else if (node.op==Bool::any) {
            for (size_t child:node.children) { end=std::min(end,visit(child,start)); if (end==start) break; }
        } else if (node.op==Bool::sequence) {
            end=start;
            for (size_t child:node.children) { end=visit(child,end); if (end==missing) break; }
        } else if (node.op==Bool::all) throw std::invalid_argument("unordered conjunction in positional DAG");
        memo[id].emplace(start,end); ++entries;
        work.peak_memo_entries=std::max(work.peak_memo_entries,entries);
        return end;
    };
    return visit(f.root,0)!=missing;
}
std::string render(const Factored& f) {
    std::ostringstream out;
    for (size_t id:f.reachable()) {
        const auto& n=f.nodes[id]; out<<id<<'=';
        if (n.op==Bool::atom) { Plan p; p.branches={n.atom}; out<<lowering::render(p); }
        else if (n.op==Bool::no || n.op==Bool::yes) out<<(n.op==Bool::yes ? "TRUE" : "FALSE");
        else { out<<(n.op==Bool::sequence ? "SEQ(" : n.op==Bool::all ? "AND(" : "OR("); for (size_t i=0;i<n.children.size();++i) out<<(i ? "," : "")<<n.children[i]; out<<')'; }
        out<<"; ";
    }
    out<<"root="<<f.root; return out.str();
}
} // namespace lowering
