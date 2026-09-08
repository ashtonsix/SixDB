#include "factor.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace lowering;
using Count=uint64_t;
std::string csv(std::string_view s) {
    std::string out="\""; for (char c:s) { if (c=='\"') out+='\"'; out+=c; } return out+'\"';
}
uint32_t u32(std::istream& f) {
    unsigned char b[4]; f.read(reinterpret_cast<char*>(b),4);
    if (!f) throw std::runtime_error("truncated input");
    return unsigned(b[0])|(unsigned(b[1])<<8)|(unsigned(b[2])<<16)|(unsigned(b[3])<<24);
}
std::string unhex(const std::string& s) {
    if (s.size()%2) throw std::runtime_error("odd hex input");
    std::string out;
    for (size_t i=0;i<s.size();i+=2) out+=char(std::stoul(s.substr(i,2),nullptr,16));
    return out;
}
bool flat(const Plan& p,std::string_view s,Work& w) {
    if (!p.useful()) return true;
    for (const auto& g:p.branches) {
        ++w.atoms; w.offered_bytes+=s.size();
        if (like(g,s,p.latin1)) return true;
    }
    return false;
}
int main(int argc,char** argv) try {
    if (argc!=4) throw std::runtime_error("usage: factor_probe DATASET INPUT_DIRECTORY OUTPUT_DIRECTORY");
    std::string dataset=argv[1],input=argv[2],output=argv[3];
    std::ifstream data(input+"/"+dataset+".strings",std::ios::binary);
    const size_t n=u32(data), warmup=std::min(n,size_t(1024));
    if (!n) throw std::runtime_error("empty input");
    std::vector<std::string> strings; Count bytes=0;
    for (size_t i=0;i<n;++i) {
        std::string s(u32(data),'\0'); data.read(s.data(),std::streamsize(s.size()));
        if (!data) throw std::runtime_error("truncated string");
        bytes+=s.size(); strings.push_back(std::move(s));
    }
    if (data.peek()!=EOF) throw std::runtime_error("trailing input");
    std::ofstream stats(output+"/"+dataset+"-factor-patterns.csv");
    stats<<"dataset,id,group,exact,rows,bytes,hits,like_re2,rich_re2,literal_re2,chain_re2,joint_like_re2,joint_rich_re2,like_atoms,like_offered_bytes,literal_atoms,literal_offered_bytes,chain_atoms,chain_offered_bytes,trained_atoms,trained_offered_bytes,warm_rows,warm_atoms,source_warm_atoms,tail_source_atoms,tail_trained_atoms,cascade_atoms,cascade_offered_bytes,reverse_atoms,reverse_offered_bytes,chain_nodes,chain_leaves,chain_tokens,chain_literal_bytes,chain_relaxations,literal_nodes,literal_leaves,literal_relaxations,like_branches,like_tokens,like_literal_bytes,chain_cache_hits,chain_node_visits,baseline_fallback,chain_fallback,literal_fallback,ordered_re2,ordered_joint_like_re2,ordered_joint_rich_re2,position_atoms,position_visits,position_node_visits,position_cache_hits,position_peak_buffer_bytes,position_peak_memo_entries,ordered_nodes,ordered_leaves,ordered_relaxations,ordered_cascade_atoms,ordered_cascade_offered_bytes,pattern,like,literal_dag,chain_dag,ordered_dag\n";
    std::ofstream outcomes(output+"/"+dataset+"-factor-outcomes.bin",std::ios::binary);
    std::ifstream queries(input+"/"+dataset+".patterns");
    std::string line; size_t done=0; Count comparisons=0;
    while (std::getline(queries,line)) {
        std::istringstream fields(line); std::string id,group,flag,hex;
        if (!std::getline(fields,id,'\t') || !std::getline(fields,group,'\t') ||
            !std::getline(fields,flag,'\t') || !std::getline(fields,hex)) throw std::runtime_error("malformed pattern");
        auto pattern=unhex(hex); auto p=compile(pattern,flag=="1",false,64);
        if (!p.oracle->ok()) throw std::runtime_error("unsupported pattern: "+id);
        auto literals=factor(p,false),chains=factor(p,true),ordered=factor(p,true,4096,true);
        Count hits=0,lp=0,rp=0,litp=0,cp=0,jp=0,jr=0,op=0,ojl=0,ojr=0;
        PositionWork positional;
        Work lw,litw,cw,tw,cascade,reverse,ordered_cascade;
        Count warm_atoms=0,source_warm_atoms=0,tail_source=0,tail_trained=0;
        std::vector<uint64_t> observations(chains.nodes.size()); Order order;
        for (size_t i=0;i<n;++i) {
            const auto& s=strings[i]; bool r=reference(p,s); hits+=r; ++comparisons;
            Work a,b,c,t; bool l=flat(p,s,a);
            if (p.exact) {
                if (l!=r) throw std::runtime_error("exact mismatch: "+id);
                outcomes.put(char(r | (l<<1))); continue;
            }
            bool rich=l && rich_matches(p,s);
            bool lit=evaluate(literals,s,b),chain=evaluate(chains,s,c);
            // An unordered gate rejects cheaply; positions are retained for its
            // survivors. Counts include both stages, never just the second.
            bool ordered_result=chain && ordered_match(ordered,s,positional);
            bool trained=i<warmup ? evaluate(chains,s,t,nullptr,true,&observations) : evaluate(chains,s,t,&order);
            if (i+1==warmup) order=train_order(chains,observations,warmup);
            if (trained!=chain || (r && (!l || !rich || !lit || !chain || !ordered_result)))
                throw std::runtime_error("false negative or order mismatch: "+id+" row="+std::to_string(i)+" pattern="+pattern);
            lp+=l; rp+=rich; litp+=lit; cp+=chain; jp+=l&&chain; jr+=rich&&chain;
            op+=ordered_result; ojl+=ordered_result&&l; ojr+=ordered_result&&rich;
            lw+=a; litw+=b; cw+=c; tw+=t;
            cascade+=c; if (chain) cascade+=a;
            reverse+=a; if (l) reverse+=c;
            ordered_cascade+=c; if (ordered_result) ordered_cascade+=a;
            if (i<warmup) { warm_atoms+=t.atoms; source_warm_atoms+=c.atoms; }
            else { tail_source+=c.atoms; tail_trained+=t.atoms; }
            outcomes.put(char(r | (l<<1) | (rich<<2) | (lit<<3) | (chain<<4) | (ordered_result<<5)));
        }
        size_t chain_tokens=0,chain_text=0,like_tokens=0,like_text=0;
        for (size_t node:chains.reachable()) for (const auto& t:chains.nodes[node].atom) { ++chain_tokens; chain_text+=t.text.size(); }
        for (const auto& g:p.branches) for (const auto& t:g) { ++like_tokens; like_text+=t.text.size(); }
        stats<<dataset<<','<<id<<','<<group<<','<<p.exact<<','<<n<<','<<bytes<<','<<hits<<','<<lp<<','<<rp<<','<<litp<<','<<cp<<','<<jp<<','<<jr
             <<','<<lw.atoms<<','<<lw.offered_bytes<<','<<litw.atoms<<','<<litw.offered_bytes<<','<<cw.atoms<<','<<cw.offered_bytes
             <<','<<tw.atoms<<','<<tw.offered_bytes<<','<<(p.exact ? 0 : warmup)<<','<<warm_atoms<<','<<source_warm_atoms<<','<<tail_source<<','<<tail_trained
             <<','<<cascade.atoms<<','<<cascade.offered_bytes<<','<<reverse.atoms<<','<<reverse.offered_bytes
             <<','<<chains.reachable().size()<<','<<chains.atoms()<<','<<chain_tokens<<','<<chain_text<<','<<chains.relaxations
             <<','<<literals.reachable().size()<<','<<literals.atoms()<<','<<literals.relaxations<<','<<p.branches.size()<<','<<like_tokens<<','<<like_text
             <<','<<cw.cache_hits<<','<<cw.node_visits<<','<<(!p.exact && !p.useful())<<','<<(chains.root==1)<<','<<(literals.root==1)
             <<','<<op<<','<<ojl<<','<<ojr<<','<<positional.atoms<<','<<positional.position_visits<<','<<positional.node_visits<<','<<positional.cache_hits
             <<','<<positional.peak_buffer_bytes<<','<<positional.peak_memo_entries<<','<<ordered.reachable().size()<<','<<ordered.atoms()<<','<<ordered.relaxations
             <<','<<ordered_cascade.atoms<<','<<ordered_cascade.offered_bytes
             <<','<<csv(pattern)<<','<<csv(render(p))<<','<<csv(render(literals))<<','<<csv(render(chains))<<','<<csv(render(ordered))<<'\n';
        if (++done%100==0) std::cerr<<dataset<<": "<<done<<" patterns checked\n";
    }
    if (!done || !stats || !outcomes) throw std::runtime_error("empty or failed output");
    std::cout<<"patterns="<<done<<" pairs="<<comparisons<<" false_negatives=0 exact_mismatches=0 order_mismatches=0\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
