#include "lower.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace lowering;
using Count=uint64_t;
std::string csv(std::string_view s) {
    std::string out="\""; for (char c:s) { if (c=='\"') out+='\"'; out+=c; } return out+'\"';
}
std::string unhex(const std::string& s) {
    if (s.size()%2) throw std::runtime_error("odd hex length");
    std::string out;
    for (size_t i=0;i<s.size();i+=2) out+=char(std::stoul(s.substr(i,2),nullptr,16));
    return out;
}
uint32_t u32(std::istream& f) {
    unsigned char b[4]; f.read(reinterpret_cast<char*>(b),4);
    if (!f) throw std::runtime_error("truncated input");
    return unsigned(b[0])|(unsigned(b[1])<<8)|(unsigned(b[2])<<16)|(unsigned(b[3])<<24);
}
struct Block { Count n=0,pass=0,hits=0,bytes=0,passbytes=0; };
struct Policy { Count filters=0,re2=0,missed=0,switches=0,probes=0,verified=0; };
Policy replay(const std::vector<Block>& blocks, std::string_view mode) {
    Policy out; bool active=mode!="direct";
    double threshold=mode=="prefix05" ? .05 : mode=="prefix50" ? .5 : .2;
    Count observed=0,rejected=0;
    size_t disabled=0,low=0;
    for (size_t i=0;i<blocks.size();++i) {
        const auto& b=blocks[i];
        bool probe=mode=="periodic20" && !active && i>disabled && (i-disabled)%16==0;
        bool filter=active || probe;
        if (filter) {
            out.filters+=b.n; out.re2+=b.pass; out.verified+=b.hits;
            observed+=b.n; rejected+=b.n-b.pass; out.probes+=probe;
        } else {
            out.re2+=b.n; out.missed+=b.n-b.pass; out.verified+=b.hits;
        }
        // Decisions use only completed, actually filtered containers. The
        // missed counter above is counterfactual reporting, never policy input.
        if (i==3 && mode!="always" && mode!="direct" && double(rejected)/observed<threshold) {
            active=false; disabled=i; ++out.switches;
        } else if (mode=="periodic20" && i>=4 && filter) {
            double rate=double(b.n-b.pass)/b.n;
            if (probe) {
                if (rate>=.4) { active=true; low=0; ++out.switches; }
            } else {
                low=rate<.2 ? low+1 : 0;
                if (low>=2) { active=false; disabled=i; low=0; ++out.switches; }
            }
        }
    }
    return out;
}
int main(int argc,char** argv) try {
    if (argc!=4 && argc!=5) throw std::runtime_error("usage: probe DATASET INPUT_DIRECTORY OUTPUT_DIRECTORY [BRANCH_BUDGET]");
    size_t branch_budget=argc==5 ? std::stoul(argv[4]) : 8;
    // A misleading four-container prefix followed by a rejection-rich tail.
    std::vector<Block> drift(40,Block{100,0,0,0,0});
    for (size_t i=0;i<4;++i) drift[i].pass=100;
    auto once=replay(drift,"prefix20"), again=replay(drift,"periodic20");
    if (once.filters!=400 || once.re2!=4000 || once.missed!=3600 || once.switches!=1 ||
        again.filters!=2500 || again.re2!=1900 || again.missed!=1500 || again.switches!=2 || again.probes!=1)
        throw std::runtime_error("adaptive controller self-check failed");
    std::string dataset=argv[1],input=argv[2],output=argv[3];
    std::ifstream data(input+"/"+dataset+".strings",std::ios::binary);
    uint32_t n=u32(data); std::vector<std::string> strings; strings.reserve(n);
    Count totalbytes=0;
    for (size_t i=0;i<n;++i) {
        uint32_t len=u32(data); std::string s(len,'\0'); data.read(s.data(),len);
        if (!data) throw std::runtime_error("truncated string");
        totalbytes+=len; strings.push_back(std::move(s));
    }
    if (data.peek()!=EOF) throw std::runtime_error("trailing input");
    std::ofstream stats(output+"/"+dataset+"-patterns.csv");
    stats<<"dataset,id,group,status,reason,pattern,like,mandatory,branches,rows,bytes,hits,like_pass,literal_pass,rich_pass,survivor_bytes,bounded_bytes,bounded_rows,intervals,bounds_re2_calls,false_negatives,exact_mismatches,bounds_mismatches\n";
    std::ofstream containers(output+"/"+dataset+"-containers.csv");
    containers<<"dataset,id,container,rows,hits,like_pass,rich_pass,bytes,like_pass_bytes\n";
    std::ofstream policies(output+"/"+dataset+"-policies.csv");
    policies<<"dataset,id,arm,order,container_rows,policy,rows,hits,filters,re2_calls,missed_rejections,switches,probe_containers\n";
    std::ofstream outcomes(output+"/"+dataset+"-outcomes.bin",std::ios::binary);
    std::vector<size_t> shuffled(n); std::iota(shuffled.begin(),shuffled.end(),0);
    std::mt19937 rng(20260908); std::shuffle(shuffled.begin(),shuffled.end(),rng);
    std::ifstream queries(input+"/"+dataset+".patterns");
    std::string line; size_t done=0;
    while (std::getline(queries,line)) {
        std::istringstream fields(line); std::string id,group,flag,hex;
        if (!std::getline(fields,id,'\t') || !std::getline(fields,group,'\t') ||
            !std::getline(fields,flag,'\t') || !std::getline(fields,hex)) throw std::runtime_error("malformed pattern");
        auto pattern=unhex(hex); auto p=compile(pattern,flag=="1",false,branch_budget);
        if (!p.oracle->ok()) {
            stats<<dataset<<','<<id<<','<<group<<",rejected,"<<csv(p.reason)<<','<<csv(pattern)<<",,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
            continue;
        }
        Count hits=0,passes=0,literals=0,richpasses=0,survivorbytes=0,boundedbytes=0,boundedrows=0,intervals=0,boundcalls=0;
        std::vector<uint8_t> flags(n);
        for (size_t i=0;i<n;++i) {
            const auto& s=strings[i]; bool r=reference(p,s),l=matches(p,s);
            bool rich=l && rich_matches(p,s);
            bool literal=p.mandatory.empty() || s.find(p.mandatory)!=s.npos;
            if ((r && (!l || !rich || !literal)) || (p.exact && r!=l))
                throw std::runtime_error("lowering mismatch: "+id+" row="+std::to_string(i)+" regexp="+pattern+" LIKE="+render(p));
            hits+=r; passes+=l; literals+=literal; richpasses+=rich;
            flags[i]=uint8_t(r | (l<<1) | (literal<<2) | (rich<<3));
            if (l && !p.exact) {
                auto ws=bounds(p,s); auto bytes=coverage(ws);
                survivorbytes+=s.size(); boundedbytes+=bytes; boundedrows+=bytes<s.size(); intervals+=ws.size();
                bool result=false;
                for (auto [lo,hi]:ws) { ++boundcalls; if (reference(p,std::string_view(s).substr(lo,hi-lo))) { result=true; break; } }
                if (r!=result) throw std::runtime_error("bounds mismatch: "+id+" row="+std::to_string(i));
            }
        }
        outcomes.write(reinterpret_cast<const char*>(flags.data()),std::streamsize(flags.size()));
        std::string status=p.exact ? "exact" : p.useful() ? "signature" : "fallback";
        stats<<dataset<<','<<id<<','<<group<<','<<status<<','<<csv(p.reason)<<','<<csv(pattern)<<','<<csv(render(p))<<','<<csv(p.mandatory)<<','<<p.branches.size()<<','<<n<<','<<totalbytes<<','<<hits<<','<<passes<<','<<literals<<','<<richpasses<<','<<survivorbytes<<','<<boundedbytes<<','<<boundedrows<<','<<intervals<<','<<boundcalls<<",0,0,0\n";
        for (size_t begin=0;begin<n;begin+=256) {
            Block b; Count rp=0;
            for (size_t i=begin;i<std::min(size_t(n),begin+256);++i) {
                ++b.n; b.hits+=flags[i]&1; b.pass+=bool(flags[i]&2); rp+=bool(flags[i]&8);
                b.bytes+=strings[i].size(); if (flags[i]&2) b.passbytes+=strings[i].size();
            }
            containers<<dataset<<','<<id<<','<<begin/256<<','<<b.n<<','<<b.hits<<','<<b.pass<<','<<rp<<','<<b.bytes<<','<<b.passbytes<<'\n';
        }
        // Exact replacements do not need a residual call and are reported
        // separately. Policies below investigate the signature route only.
        if (!p.exact) for (auto [arm,mask]:{std::pair{"like",2},std::pair{"rich",8}}) {
            bool useful=p.useful();
            if (mask==8) {
                useful=true;
                for (const auto& g:p.branches) if (g.size()==1 && g[0].kind==Kind::many &&
                    g[0].maximum==infinity && g[0].ranges.empty()) useful=false;
            }
            for (std::string order:{"source","shuffled","survivors-first"}) {
                std::vector<size_t> indices(n); std::iota(indices.begin(),indices.end(),0);
                if (order=="shuffled") indices=shuffled;
                if (order=="survivors-first") std::stable_partition(indices.begin(),indices.end(),[&](size_t i){return flags[i]&mask;});
                for (size_t blocksize:{64,256,1024}) {
                    std::vector<Block> blocks;
                    for (size_t i=0;i<n;++i) {
                        if (i%blocksize==0) blocks.emplace_back();
                        auto& b=blocks.back(); auto f=flags[indices[i]];
                        ++b.n; b.pass+=bool(f&mask); b.hits+=f&1;
                    }
                    for (std::string policy:{"direct","always","prefix05","prefix20","prefix50","periodic20"}) {
                        // Statically known pass-all filters are bypassed in
                        // every arm; adaptation must earn more than that control.
                        auto q=replay(blocks,useful ? policy : "direct");
                        if (q.verified!=hits) throw std::runtime_error("adaptive result mismatch");
                        policies<<dataset<<','<<id<<','<<arm<<','<<order<<','<<blocksize<<','<<policy<<','<<n<<','<<hits<<','<<q.filters<<','<<q.re2<<','<<q.missed<<','<<q.switches<<','<<q.probes<<'\n';
                    }
                }
            }
        }
        if (++done%100==0) std::cerr<<dataset<<": "<<done<<" patterns checked\n";
    }
    if (!done || !stats || !policies || !containers || !outcomes) throw std::runtime_error("empty or failed output");
    std::cout<<dataset<<": "<<done<<" supported patterns x "<<n<<" raw strings; all comparisons agree\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
