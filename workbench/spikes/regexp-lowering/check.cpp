#include "lower.h"
#include <iostream>
#include <functional>
#include <random>
#include <stdexcept>

using namespace lowering;
void require(bool ok,const std::string& why) { if (!ok) throw std::runtime_error(why); }
int main() try {
    size_t pairs=0,positives=0,exact=0;
    std::vector<std::string> patterns={"", "^$", "foo", "^foo", "foo$", "^foo$",
        "a.*b", "(?s)a.*b", "(?s)^a.*a$", "a.b", "(?s)a.b", "a+", "a*", "(a|b)?c",
        "foo(bar)?baz", "foo|[0-9]+", "(fatal|error).*id=[0-9]+", "\\bcat\\b", "(?m)^a$",
        "(?i)foo", "(?i:a)b", "a%b_c!", "id=[0-9]{4};", "[ab]{2,4}x", "a{0,30}b",
        "(ab)+c", "(?:foo|bar)\\d{1,3}Z", "a^b", "a$b", "^a|b$", "^", "$", "é.Ω",
        "\\C\\Cfoo", "[^a]+b", "[0-9]*a", "(?:[a-z]x){1,3}end", "[ab]{0,20}c",
        "[ab]{20}c", "(?s).{0,30}a", "(?s).{20}b", "(a|b|c|d|e|f|g|h|i)z"};
    std::vector<std::string> strings={"foo", "bar", "foobaz", "foobarbaz", "id=xxxx; ... id=1234;",
        "xid=1234;", "id=1234;", "a\nb", "éaΩ", "é\nΩ", "éΩfoo", "foo123Z", "bar9Z",
        "error xx id=123", "fatal\nid=3", "a%b_c!", ""};
    std::function<void(std::string,int)> enumerate=[&](std::string s,int left) {
        strings.push_back(s); if (!left) return;
        for (char c:std::string("ab\n%_!\0",7)) enumerate(s+c,left-1);
    };
    enumerate("",4);
    std::mt19937 random(20260908);
    const std::vector<std::string> atoms={"a", "b", "[0-9]", "[ab]", ".", "(?s:.)", "é", "(?:x|yz)", "(?i:k)"};
    const std::vector<std::string> quant={"", "?", "*", "+", "{2}", "{1,3}", "{0,20}"};
    for (int i=0;i<240;++i) {
        std::string p;
        for (int j=0,n=1+random()%4;j<n;++j) p+="(?:"+atoms[random()%atoms.size()]+")"+quant[random()%quant.size()];
        if (i%5==0) p="^"+p+"$";
        patterns.push_back(p);
    }
    for (bool latin1:{false,true}) for (size_t budget:{size_t(2),size_t(8)}) for (const auto& pattern:patterns) {
        auto p=compile(pattern,false,latin1,budget);
        require(p.oracle->ok(),"fixture pattern rejected: "+pattern);
        exact+=p.exact;
        for (const auto& s:strings) {
            bool r=reference(p,s), l=matches(p,s), rich=rich_matches(p,s);
            bool slow=false;
            for (const auto& g:p.branches) slow |= like_oracle(g,s,latin1);
            std::string where="pattern="+pattern+" subject="+s+" like="+render(p);
            require(l==slow,"LIKE disagreement: "+where);
            require(!r || l,"LIKE false negative: "+where);
            require(!r || rich,"rich IR false negative: "+where);
            require(!rich || l,"rich IR not contained in LIKE: "+where);
            require(!p.exact || r==l,"inexact replacement: "+where);
            if (l) {
                auto ws=bounds(p,s,4);
                require(verify_bounds(p,s,ws)==r,"bounds mismatch: "+where);
                require(coverage(ws)<=s.size(),"overlapping coverage");
            }
            ++pairs; positives+=r;
        }
    }
    for (const auto& s:{"(", "[z-a]", "a{1001}", "(?=a)", "(a)\\1"}) require(!compile(s).oracle->ok(),"invalid accepted");
    // Explicit positive and expected-route checks catch vacuous comparisons.
    require(compile("needle").exact,"literal should be exact");
    require(compile("(?s)^a.*b$").exact,"dot-all chain should be exact");
    require(!compile("^a.*b$").exact,"newline restriction cannot be exact LIKE");
    auto p=compile("id=[0-9]{4};");
    std::string s="prefix id=xxxx; padding id=1234; suffix";
    auto ws=bounds(p,s);
    require(coverage(ws)<s.size() && verify_bounds(p,s,ws),"useful multi-witness bounds");
    std::cout<<"pairs="<<pairs<<" positive_pairs="<<positives<<" exact_plans="<<exact
             <<" LIKE_mismatches=0 false_negatives=0 bounds_mismatches=0\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
