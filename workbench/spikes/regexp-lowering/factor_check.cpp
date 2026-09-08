#include "factor.h"
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <tuple>

using namespace lowering;
void require(bool ok,const std::string& why) { if (!ok) throw std::runtime_error(why); }
// Independently retain every feasible endpoint through every sequence. This
// checks the earliest-completion shortcut, not only the leaf DP implementation.
bool all_endpoints(const Factored& f,std::string_view s) {
    using Ends=std::vector<size_t>;
    std::map<std::pair<size_t,size_t>,Ends> memo;
    std::function<Ends(size_t,size_t)> visit=[&](size_t id,size_t start) {
        auto key=std::pair{id,start};
        if (auto it=memo.find(key); it!=memo.end()) return it->second;
        const auto& node=f.nodes[id]; Ends ends;
        if (node.op==Bool::yes) ends={start};
        else if (node.op==Bool::atom) {
            for (size_t end=start;end<=s.size();++end) {
                if (!f.latin1 && end<s.size() && (static_cast<unsigned char>(s[end])&0xc0)==0x80) continue;
                if (like_oracle(node.atom,s.substr(start,end-start),f.latin1)) ends.push_back(end);
            }
        } else if (node.op==Bool::any) {
            for (size_t child:node.children) { auto part=visit(child,start); ends.insert(ends.end(),part.begin(),part.end()); }
        } else if (node.op==Bool::sequence) {
            ends={start};
            for (size_t child:node.children) {
                Ends following;
                for (size_t end:ends) { auto part=visit(child,end); following.insert(following.end(),part.begin(),part.end()); }
                std::sort(following.begin(),following.end()); following.erase(std::unique(following.begin(),following.end()),following.end());
                ends=std::move(following);
            }
        }
        std::sort(ends.begin(),ends.end()); ends.erase(std::unique(ends.begin(),ends.end()),ends.end());
        memo.emplace(key,ends); return ends;
    };
    return !visit(f.root,0).empty();
}
int main() try {
    std::vector<std::string> patterns={"", "^$", "foo", "foo.*bar", "(?s)^foo.*bar$",
        "(?:foo.*bar|baz.*qux)", "(?:foo|bar).*(?:baz|qux)", "foo|[0-9]+", "foo(?:bar)?",
        "(?i)foo", "(?i:foo)BAR", "\\bcat\\b", "(?m)^a$", "é.Ω", "\\C\\Cfoo",
        "a^b", "a$b", "[0-9]{4}", "[ab]{20}", "(?:foo|bar){2,4}", "(?:foo|bar)*x",
        "foo%_!", "(?:alpha.*beta|alpha.*gamma)", "(foo.*bar)+baz", "(foo.*bar){0,30}",
        "[^a]+b", "(?s).{20}b", "a{1000}b", "[ab]{0}", "\\C(?:x)?\\Cfoo"};
    std::vector<std::string> strings={"", "foo", "foobar", "barfoo", "fooxbar", "bazqux",
        "bar xx baz", "foo\nbar", "fooqux", "alpha beta", "alpha gamma", "beta alpha",
        "FOOBAR", "cat", "scat", "éaΩ", "éΩfoo", "1234", "foo%_!", "afoo", "éfoo"};
    std::function<void(std::string,int)> enumerate=[&](std::string s,int left) {
        strings.push_back(s); if (!left) return;
        for (char c:std::string("ab\n_\0",5)) enumerate(s+c,left-1);
    };
    enumerate("",4);
    std::mt19937 rng(20260909);
    std::vector<std::string> atoms={"a", "[ab]", "[0-9]", ".", "(?s:.)", "é", "(?:foo|bar)", "(?i:k)"};
    std::vector<std::string> quant={"", "?", "*", "+", "{2}", "{1,3}", "{0,20}"};
    for (int i=0;i<160;++i) {
        std::string p;
        for (int j=0,n=1+int(rng()%4);j<n;++j) p+="(?:"+atoms[rng()%atoms.size()]+")"+quant[rng()%quant.size()];
        if (i%5==0) p="^"+p+"$";
        patterns.push_back(p);
    }
    uint64_t pairs=0,positive=0,ordered_pairs=0,ordered_positive=0,endpoint_pairs=0;
    for (bool latin1:{false,true}) for (const auto& pattern:patterns) {
        auto p=compile(pattern,false,latin1,64); require(p.oracle->ok(),"invalid fixture");
        for (bool chains:{false,true}) for (size_t budget:{size_t(2),size_t(8),size_t(4096)}) {
            auto f=factor(p,chains,budget);
            require(f.nodes.size()<=budget,"node budget");
            std::vector<uint64_t> counts(f.nodes.size());
            for (const auto& s:strings) {
                Work a,b; bool fast=evaluate(f,s,a),slow=evaluate(f,s,b,nullptr,true,&counts,true);
                bool r=reference(p,s);
                require(fast==slow,"DAG/LIKE disagreement: "+pattern);
                require(!r || fast,"false negative: "+pattern+" subject="+s+" plan="+render(f));
                require(a.atoms<=f.atoms() && b.atoms==f.atoms(),"memo/eager accounting");
                require(a.offered_bytes==a.atoms*s.size(),"offered-byte accounting");
                ++pairs; positive+=r;
            }
            auto order=train_order(f,counts,strings.size());
            for (const auto& s:strings) {
                Work a,b; require(evaluate(f,s,a)==evaluate(f,s,b,&order),"reordering changed result");
            }
        }
        for (size_t budget:{size_t(2),size_t(8),size_t(4096)}) {
            auto f=factor(p,true,budget,true);
            for (const auto& s:strings) {
                PositionWork a,b;
                bool fast=ordered_match(f,s,a),slow=ordered_match(f,s,b,true),r=reference(p,s);
                require(fast==slow,"endpoint disagreement: "+pattern+" subject="+s);
                require(!r || fast,"ordered false negative: "+pattern+" subject="+s+" plan="+render(f));
                if (budget==4096 && (s.size()<=3 || s.size()>4)) {
                    require(fast==all_endpoints(f,s),"earliest endpoint lost a feasible continuation: "+pattern+" subject="+s);
                    ++endpoint_pairs;
                }
                ++ordered_pairs; ordered_positive+=r;
            }
        }
    }
    auto p=compile("foo.*bar"); auto literal=factor(p,false),chain=factor(p,true);
    Work w;
    require(evaluate(literal,"barfoo",w) && !evaluate(chain,"barfoo",w),"local order witness");
    p=compile("(?:foo.*bar|baz.*qux)"); chain=factor(p,true);
    require(!evaluate(chain,"fooqux",w),"OR branch correlation lost");
    p=compile("(?:alpha.*beta|alpha.*gamma)"); literal=factor(p,false);
    Work short_work,eager_work;
    require(!evaluate(literal,"neither",short_work),"negative witness");
    evaluate(literal,"neither",eager_work,nullptr,true);
    require(short_work.atoms<eager_work.atoms,"short circuit must save atoms");
    // Reordering must pay for training and use only its supplied observations.
    Factored explicit_dag;
    explicit_dag.nodes.push_back({Bool::atom,{},{{Kind::many,{},infinity},{Kind::literal,"common",6},{Kind::many,{},infinity}}});
    explicit_dag.nodes.push_back({Bool::atom,{},{{Kind::many,{},infinity},{Kind::literal,"rare",4},{Kind::many,{},infinity}}});
    explicit_dag.nodes.push_back({Bool::all,{2,3}}); explicit_dag.root=4;
    std::vector<uint64_t> counts(explicit_dag.nodes.size()); Work warm;
    for (int i=0;i<4;++i) evaluate(explicit_dag,"common",warm,nullptr,true,&counts);
    auto order=train_order(explicit_dag,counts,4); Work source,trained;
    evaluate(explicit_dag,"common",source); evaluate(explicit_dag,"common",trained,&order);
    require(warm.atoms==8 && source.atoms==2 && trained.atoms==1 && order[4][0]==3,"training accounting");
    for (const auto& [pattern,subject,expected]:std::vector<std::tuple<std::string,std::string,bool>>{
        {"(?:foo|bar).*(?:baz|qux)","qux foo",false},
        {"(?:foo|bar).*(?:baz|qux)","foo qux",true},
        {"(?:aba|xyz).*(?:aba|xyz)","ababa",false},
        {"(?:aba|xyz).*(?:aba|xyz)","aba xyz",true},
        {"(?:aba|xyz){2}","ababa",false},
        {"(?:aba|xyz){2}","abaxyz",true},
        {"(?:ab.*z|b).*c","ab c z",true},
        {"(?:foo.*bar|baz.*qux)","foo qux",false}}) {
        auto original=compile(pattern); auto ordered=factor(original,true,4096,true); PositionWork pos;
        require(reference(original,subject)==expected && ordered_match(ordered,subject,pos)==expected,"directed relative-order witness");
    }
    bool rejected=false;
    try { factor(compile("("),true); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"invalid regexp must not become a filter");
    std::cout<<"pairs="<<pairs<<" positive_pairs="<<positive<<" ordered_pairs="<<ordered_pairs<<" ordered_positive_pairs="<<ordered_positive<<" all_endpoint_pairs="<<endpoint_pairs
             <<" false_negatives=0 DAG_mismatches=0 order_mismatches=0 accounting_mismatches=0\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
