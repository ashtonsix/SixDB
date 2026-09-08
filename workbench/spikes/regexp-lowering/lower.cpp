#include "lower.h"
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
#include <re2/regexp.h>

namespace lowering {
namespace {
size_t add(size_t a, size_t b) { return a >= infinity-b ? infinity : a+b; }
size_t mul(size_t a, size_t b) { return b && a >= infinity/b ? infinity : a*b; }
size_t next(std::string_view s, size_t p, bool latin1) {
    if (p >= s.size()) return s.size()+1;
    ++p;
    if (!latin1) while (p<s.size() && (static_cast<unsigned char>(s[p])&0xc0)==0x80) ++p;
    return p;
}
std::string rune(int r, bool latin1) {
    if (latin1 || r<128) return std::string(1, char(r));
    std::string out;
    if (r<0x800) out += char(0xc0|(r>>6));
    else if (r<0x10000) { out += char(0xe0|(r>>12)); out += char(0x80|((r>>6)&63)); }
    else { out += char(0xf0|(r>>18)); out += char(0x80|((r>>12)&63)); out += char(0x80|((r>>6)&63)); }
    out += char(0x80|(r&63));
    return out;
}
Token many(size_t maximum=infinity) { return {Kind::many, {}, maximum}; }
Glob normalize(Glob g) {
    Glob out;
    for (auto& t:g) {
        if (t.kind==Kind::literal && t.text.empty()) continue;
        if (!out.empty() && t.kind==out.back().kind &&
            (t.kind==Kind::literal || (t.kind==Kind::many && t.ranges==out.back().ranges))) {
            out.back().text += t.text;
            out.back().maximum=add(out.back().maximum,t.maximum);
        } else out.push_back(std::move(t));
    }
    return out;
}
struct Info { std::vector<Glob> branches{{}}; size_t maximum=0; };
struct Builder {
    Plan& plan;
    size_t budget;
    size_t visited=0;
    void weaken(std::string_view why) {
        plan.exact=false;
        if (plan.reason.find(why)==std::string::npos) {
            if (!plan.reason.empty()) plan.reason += ";";
            plan.reason += why;
        }
    }
    Info gap(size_t maximum, std::string_view why) {
        weaken(why);
        return {{{many(maximum)}},maximum};
    }
    Info concat(Info a, const Info& b) {
        size_t width=add(a.maximum,b.maximum);
        if (a.branches.empty() || b.branches.empty()) return {{},width};
        if (a.branches.size()*b.branches.size()>budget) {
            // Widen this entire concatenation; parents retain their own literals.
            return gap(width,"branch-budget");
        }
        Info out{{},width};
        for (const auto& x:a.branches) for (const auto& y:b.branches) {
            if (x.size()+y.size()>512) return gap(width,"token-budget");
            Glob g=x; g.insert(g.end(),y.begin(),y.end());
            out.branches.push_back(normalize(std::move(g)));
        }
        return out;
    }
    Info build(re2::Regexp* r, size_t depth=0) {
        using namespace re2;
        if (++visited>10000 || depth>128) { plan.context=true; return gap(infinity,"ast-budget"); }
        const size_t unit=plan.latin1 ? 1 : 4;
        auto child=[&](int i) { return build(r->sub()[i],depth+1); };
        switch (r->op()) {
        case kRegexpNoMatch: return {{},0};
        case kRegexpEmptyMatch: return {};
        case kRegexpCapture: return child(0);
        case kRegexpLiteral:
        case kRegexpLiteralString: {
            size_t n=r->op()==kRegexpLiteral ? 1 : size_t(r->nrunes());
            if (r->parse_flags() & Regexp::FoldCase) return gap(mul(n,unit),"case-fold");
            std::string s;
            for (size_t i=0;i<n;++i) s += rune(r->op()==kRegexpLiteral ? r->rune() : r->runes()[i],plan.latin1);
            return {{{{Kind::literal,s,s.size()}}},s.size()};
        }
        case kRegexpAnyChar:
            return {{{{Kind::one,{},unit}}},unit}; // Parser represents newline exclusion as a class.
        case kRegexpAnyByte:
            if (plan.latin1) return {{{{Kind::one,{},1}}},1};
            plan.context=true;
            return gap(1,"byte-in-utf8");
        case kRegexpCharClass: {
            size_t count=0;
            std::vector<std::pair<int,int>> ranges;
            size_t maxwidth=0;
            for (auto range:*r->cc()) {
                count += size_t(range.hi-range.lo)+1;
                ranges.emplace_back(range.lo,range.hi);
                maxwidth=std::max(maxwidth,rune(range.hi,plan.latin1).size());
            }
            if (count<=4 && count<=budget) {
                Info out{{},maxwidth};
                for (auto range:*r->cc()) for (int v=range.lo;v<=range.hi;++v) {
                    auto s=rune(v,plan.latin1);
                    out.branches.push_back({{Kind::literal,s,s.size()}});
                }
                return out;
            }
            weaken("character-class");
            return {{{{Kind::one,{},maxwidth,ranges}}},maxwidth};
        }
        case kRegexpBeginText:
        case kRegexpEndText:
            plan.context=true;
            return {{{{r->op()==kRegexpBeginText ? Kind::begin : Kind::end,{},0}}},0};
        case kRegexpBeginLine: case kRegexpEndLine:
        case kRegexpWordBoundary: case kRegexpNoWordBoundary:
            plan.context=true; weaken("assertion"); return {};
        case kRegexpConcat: {
            Info out;
            for (int i=0;i<r->nsub();++i) out=concat(std::move(out),child(i));
            return out;
        }
        case kRegexpAlternate: {
            Info out{{},0};
            for (int i=0;i<r->nsub();++i) {
                auto b=child(i); out.maximum=std::max(out.maximum,b.maximum);
                out.branches.insert(out.branches.end(),b.branches.begin(),b.branches.end());
            }
            if (out.branches.size()>budget) return gap(out.maximum,"branch-budget");
            return out;
        }
        case kRegexpStar: case kRegexpPlus: case kRegexpQuest: case kRegexpRepeat: {
            auto a=child(0);
            int lo=r->op()==kRegexpPlus ? 1 : r->op()==kRegexpRepeat ? r->min() : 0;
            int hi=r->op()==kRegexpQuest ? 1 : r->op()==kRegexpRepeat ? r->max() : -1;
            size_t width=hi<0 ? infinity : mul(a.maximum,size_t(hi));
            if (hi<0 && a.branches.size()==1 && a.branches[0].size()==1 &&
                a.branches[0][0].kind==Kind::one) {
                Glob g;
                for (int i=0;i<lo;++i) g.push_back(a.branches[0][0]);
                auto tail=many(); tail.ranges=a.branches[0][0].ranges;
                g.push_back(tail); return {{g},infinity};
            }
            if (hi>=0 && hi<=16) {
                Info out{{},width}, part;
                for (int n=0;n<=hi;++n) {
                    if (n>=lo) out.branches.insert(out.branches.end(),part.branches.begin(),part.branches.end());
                    if (out.branches.size()>budget) return gap(width,"repeat-budget");
                    if (n<hi) part=concat(std::move(part),a);
                }
                return out;
            }
            if (lo==0) return gap(width,"optional-repeat");
            // At least one child occurs; retain it, then relax remaining copies.
            weaken("unbounded-repeat");
            return concat(std::move(a),{{{many(width)}},width});
        }
        default: plan.context=true; return gap(infinity,"unknown-op");
        }
    }
};
} // namespace

Plan compile(const std::string& pattern, bool insensitive, bool latin1, size_t branch_budget) {
    if (!branch_budget) throw std::invalid_argument("branch budget must be positive");
    Plan p; p.latin1=latin1;
    RE2::Options options;
    options.set_encoding(latin1 ? RE2::Options::EncodingLatin1 : RE2::Options::EncodingUTF8);
    options.set_case_sensitive(!insensitive); options.set_log_errors(false);
    p.oracle=std::make_unique<RE2>(pattern,options);
    if (!p.oracle->ok()) { p.reason=p.oracle->error(); return p; }
    Builder builder{p,branch_budget};
    auto info=builder.build(p.oracle->Regexp());
    for (auto g:info.branches) {
        g=normalize(std::move(g));
        bool start=!g.empty() && g.front().kind==Kind::begin;
        bool end=!g.empty() && g.back().kind==Kind::end;
        if (end) g.pop_back();
        if (start) g.erase(g.begin());
        Glob cleaned;
        for (auto& t:g) {
            if (t.kind==Kind::begin || t.kind==Kind::end) builder.weaken("internal-anchor");
            else cleaned.push_back(t);
        }
        g=normalize(std::move(cleaned)); p.regions.push_back(g);
        if (!start) g.insert(g.begin(),many());
        if (!end) g.push_back(many());
        p.branches.push_back(normalize(std::move(g)));
    }
    // A conservative single mandatory literal common to every alternative.
    if (!p.branches.empty()) for (const auto& t:p.branches.front()) {
        if (t.kind!=Kind::literal || t.text.size()<=p.mandatory.size()) continue;
        bool common=true;
        for (const auto& branch:p.branches) {
            bool found=false;
            for (const auto& u:branch) if (u.kind==Kind::literal && u.text.find(t.text)!=std::string::npos) found=true;
            common &= found;
        }
        if (common) p.mandatory=t.text;
    }
    return p;
}
bool Plan::useful() const {
    for (const auto& g:branches) if (g.size()==1 && g[0].kind==Kind::many) return false;
    return true;
}
bool like(const Glob& g, std::string_view s, bool latin1) {
    size_t i=0,p=0,star=size_t(-1),retry=0;
    while (true) {
        if (i==g.size()) { if (p==s.size()) return true; }
        else if (g[i].kind==Kind::many) { star=i++; retry=p; continue; }
        else if (g[i].kind==Kind::literal && s.substr(p).starts_with(g[i].text)) {
            p+=g[i++].text.size(); continue;
        } else if (g[i].kind==Kind::one && p<s.size()) { p=next(s,p,latin1); ++i; continue; }
        if (star==size_t(-1) || retry>=s.size()) return false;
        retry=next(s,retry,latin1); p=retry; i=star+1;
    }
}
// Independent dynamic-programming LIKE implementation for differential checks.
bool like_oracle(const Glob& g, std::string_view s, bool latin1) {
    std::vector<uint8_t> current(s.size()+1),following(s.size()+1); current[0]=1;
    for (const auto& t:g) {
        std::fill(following.begin(),following.end(),0);
        for (size_t p=0;p<=s.size();++p) if (current[p]) {
            if (t.kind==Kind::many) {
                for (size_t q=p;q<=s.size();q=next(s,q,latin1)) following[q]=1;
            } else if (t.kind==Kind::one && p<s.size()) following[next(s,p,latin1)]=1;
            else if (t.kind==Kind::literal && s.substr(p).starts_with(t.text)) following[p+t.text.size()]=1;
        }
        current.swap(following);
    }
    return current[s.size()];
}
bool matches(const Plan& p, std::string_view s) {
    return std::any_of(p.branches.begin(),p.branches.end(),[&](const auto& g){return like(g,s,p.latin1);});
}
bool rich_matches(const Plan& plan, std::string_view s) {
    // Reference DP, deliberately untimed. Range transitions and finite gap
    // widths are regular constraints; no compressed backend is claimed here.
    auto permitted=[&](const Token& t,size_t p) {
        if (t.ranges.empty()) return true;
        unsigned c=static_cast<unsigned char>(s[p]); int v=int(c);
        if (!plan.latin1 && c>=128) {
            unsigned count=c<224 ? 2 : c<240 ? 3 : 4;
            v=int(c & (0x7f>>count));
            for (unsigned j=1;j<count;++j) v=(v<<6)|(static_cast<unsigned char>(s[p+j])&63);
        }
        for (auto [lo,hi]:t.ranges) if (v>=lo && v<=hi) return true;
        return false;
    };
    for (const auto& g:plan.branches) {
        std::vector<uint8_t> current(s.size()+1),following(s.size()+1); current[0]=1;
        for (const auto& t:g) {
            std::fill(following.begin(),following.end(),0);
            if (t.kind==Kind::many && t.maximum==infinity) {
                // Linear closure for unrestricted-length runs, respecting class.
                for (size_t pos=0;pos<=s.size();pos=next(s,pos,plan.latin1)) {
                    if (current[pos]) following[pos]=1;
                    if (following[pos] && pos<s.size() && permitted(t,pos)) following[next(s,pos,plan.latin1)]=1;
                }
            } else for (size_t pos=0;pos<=s.size();++pos) if (current[pos]) {
                if (t.kind==Kind::many) {
                    size_t q=pos; following[q]=1;
                    while (q<s.size() && permitted(t,q)) {
                        q=next(s,q,plan.latin1);
                        if (q-pos>t.maximum) break;
                        following[q]=1;
                    }
                } else if (t.kind==Kind::one && pos<s.size() && permitted(t,pos)) following[next(s,pos,plan.latin1)]=1;
                else if (t.kind==Kind::literal && s.substr(pos).starts_with(t.text)) following[pos+t.text.size()]=1;
            }
            current.swap(following);
        }
        if (current[s.size()]) return true;
    }
    return false;
}
bool reference(const Plan& p, std::string_view s) {
    return RE2::PartialMatch(re2::StringPiece(s.data(),s.size()),*p.oracle);
}
std::string render(const Plan& p) {
    std::string out;
    for (const auto& g:p.branches) {
        if (!out.empty()) out+=" OR "; out+='\'';
        for (const auto& t:g) {
            if (t.kind==Kind::one) out+='_';
            else if (t.kind==Kind::many) out+='%';
            else for (char c:t.text) {
                if (c=='%' || c=='_' || c=='!') out+='!';
                if (c=='\'') out+='\'';
                out+=c;
            }
        }
        out+="' ESCAPE '!'";
    }
    return out.empty() ? "FALSE" : out;
}
std::vector<Interval> bounds(const Plan& p, std::string_view s, size_t cap) {
    if (p.context) return {{0,s.size()}};
    std::vector<Interval> all;
    size_t occurrences=0;
    for (const auto& g:p.regions) {
        size_t best=size_t(-1);
        for (size_t i=0;i<g.size();++i) if (g[i].kind==Kind::literal &&
            (best==size_t(-1) || g[i].text.size()>g[best].text.size())) best=i;
        if (best==size_t(-1)) return {{0,s.size()}};
        size_t before=0,after=0;
        for (size_t i=0;i<best;++i) before=add(before,g[i].maximum);
        for (size_t i=best+1;i<g.size();++i) after=add(after,g[i].maximum);
        const auto& needle=g[best].text;
        for (size_t pos=s.find(needle);pos!=s.npos;pos=s.find(needle,pos+1)) {
            if (++occurrences>cap) return {{0,s.size()}};
            size_t lo=pos>before ? pos-before : 0;
            size_t hi=std::min(s.size(),add(pos+needle.size(),after));
            if (!p.latin1) {
                while (lo>0 && (static_cast<unsigned char>(s[lo])&0xc0)==0x80) --lo;
                while (hi<s.size() && (static_cast<unsigned char>(s[hi])&0xc0)==0x80) ++hi;
            }
            all.emplace_back(lo,hi);
        }
    }
    std::sort(all.begin(),all.end());
    std::vector<Interval> merged;
    for (auto w:all) {
        if (!merged.empty() && w.first<=merged.back().second) merged.back().second=std::max(merged.back().second,w.second);
        else merged.push_back(w);
    }
    return merged;
}
bool verify_bounds(const Plan& p, std::string_view s, const std::vector<Interval>& ws) {
    for (auto [lo,hi]:ws) if (reference(p,s.substr(lo,hi-lo))) return true;
    return false;
}
size_t coverage(const std::vector<Interval>& ws) {
    size_t n=0; for (auto [lo,hi]:ws) n+=hi-lo; return n;
}
} // namespace lowering
