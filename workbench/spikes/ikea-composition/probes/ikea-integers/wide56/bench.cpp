#include "api.h"
#include "comparators.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace s=ikea::integers::wide56::study;
namespace {
using U=std::uint64_t;
constexpr U mask56=(U{1}<<56)-1, step=0x9e3779b97f4a7c15ULL, data_seed=0x56b0d1e5ULL;
U mix(U x) {x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
U value(U i) {return mix(i+data_seed)&mask56;}
U now() {timespec t{};if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))throw std::runtime_error("clock");return U(t.tv_sec)*1000000000+t.tv_nsec;}
void require(bool condition,const char* why) {if(!condition)throw std::runtime_error(why);}
struct Buffer {
    std::uint8_t* p=nullptr;U bytes;
    explicit Buffer(U n):bytes(n) {void* v=nullptr;if(posix_memalign(&v,64,n))throw std::bad_alloc();p=static_cast<std::uint8_t*>(v);std::memset(p,0,n);}
    ~Buffer(){std::free(p);} Buffer(const Buffer&)=delete;
};
struct Perf {
    std::array<int,3> f{-1,-1,-1};std::string status="not_requested";
    U cycles=0,instructions=0,misses=0,enabled=0,running=0;
    explicit Perf(bool yes) {
        if(!yes)return;status="available";
        constexpr U events[]{PERF_COUNT_HW_CPU_CYCLES,PERF_COUNT_HW_INSTRUCTIONS,PERF_COUNT_HW_CACHE_MISSES};
        for(unsigned i=0;i<3;++i) {
            perf_event_attr a{};a.size=sizeof a;a.type=PERF_TYPE_HARDWARE;a.config=events[i];
            a.disabled=i==0;a.exclude_kernel=1;a.exclude_hv=1;
            a.read_format=PERF_FORMAT_GROUP|PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
            f[i]=int(syscall(SYS_perf_event_open,&a,0,-1,i?f[0]:-1,0));
            if(f[i]<0){status="unavailable_"+std::to_string(errno);close_all();break;}
        }
    }
    ~Perf(){close_all();}
    void close_all(){for(auto& fd:f){if(fd>=0)close(fd);fd=-1;}}
    void start(){if(f[0]>=0 && (ioctl(f[0],PERF_EVENT_IOC_RESET,PERF_IOC_FLAG_GROUP)||ioctl(f[0],PERF_EVENT_IOC_ENABLE,PERF_IOC_FLAG_GROUP))){status="enable_failed";close_all();}}
    void stop(){if(f[0]<0)return;struct {U count,enabled,running,values[3];}r{};
        if(ioctl(f[0],PERF_EVENT_IOC_DISABLE,PERF_IOC_FLAG_GROUP)||read(f[0],&r,sizeof r)!=sizeof r||r.count!=3){status="read_failed";return;}
        cycles=r.values[0];instructions=r.values[1];misses=r.values[2];enabled=r.enabled;running=r.running;
        if(!running)status="not_scheduled";
    }
};
struct Options {unsigned reps=5;U samples=1<<20,bulk_values=32768;std::vector<unsigned> powers{10,16,26};bool pmu=false,check_only=false;std::string suite="all";};
Options options(int argc,char** argv){Options o;
    for(int i=1;i<argc;++i){std::string a=argv[i];
        if(a=="--quick"){o.reps=1;o.samples=4096;o.bulk_values=1024;o.powers={10,16};}
        else if(a=="--pmu")o.pmu=true;
        else if(a=="--check-only")o.check_only=true;
        else if(a=="--suite" && i+1<argc)o.suite=argv[++i];
        else if(a=="--repetitions" && i+1<argc)o.reps=unsigned(std::stoul(argv[++i]));
        else if(a=="--samples" && i+1<argc)o.samples=std::stoull(argv[++i]);
        else if(a=="--bulk-values" && i+1<argc)o.bulk_values=std::stoull(argv[++i]);
        else if(a=="--powers" && i+1<argc){o.powers.clear();std::string p=argv[++i];std::size_t pos=0;
            while(pos<p.size()){std::size_t end=p.find(',',pos);o.powers.push_back(unsigned(std::stoul(p.substr(pos,end-pos))));if(end==std::string::npos)break;pos=end+1;}}
        else throw std::runtime_error("Unknown/missing option: "+a);
    }
    require(o.reps && o.samples && o.samples%8==0 && o.bulk_values>=256 && o.bulk_values%256==0 && o.bulk_values<=65536,"Invalid work counts");
    require(!o.powers.empty(),"Empty capacity powers");for(auto p:o.powers)require(p>=8 && p<=27,"Capacity power outside 8..27");
    require(o.suite=="all" || o.suite=="bulk" || o.suite=="capacity","Invalid suite");return o;
}
int pin(){cpu_set_t allowed;CPU_ZERO(&allowed);require(!sched_getaffinity(0,sizeof allowed,&allowed),"getaffinity");
    int cpu=-1;if(const char* e=std::getenv("SIXDB_CPU"))cpu=std::stoi(e);else for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&allowed)){cpu=i;break;}
    require(cpu>=0 && cpu<CPU_SETSIZE && CPU_ISSET(cpu,&allowed),"Invalid CPU");cpu_set_t one;CPU_ZERO(&one);CPU_SET(cpu,&one);
    require(!sched_setaffinity(0,sizeof one,&one) && sched_getcpu()==cpu,"pinning");return cpu;
}
const s::Arm local{"local_aos7",{wide56_local_get1,wide56_local_get16,wide56_local_decode256,wide56_local_encode256,wide56_local_sum256},s::Wire::aos7,1792};
std::array<s::Arm,4> arms(){return {local,s::prior,s::fixed_planes,s::plain};}
void fill(const s::Arm& a,Buffer& b,U n){alignas(64) U in[256];for(U base=0;base<n;base+=256){for(unsigned i=0;i<256;++i)in[i]=value(base+i);a.codec.encode(in,b.p+(base/256)*a.bytes);}}
U oracle(const s::Arm& a,const std::uint8_t* p,unsigned i){
    if(a.wire==s::Wire::planes){U x=(U(p[i])<<48)|(U(p[256+i])<<40)|(U(p[512+i])<<32);for(unsigned b=0;b<4;++b)x|=U(p[768+4*i+b])<<(8*b);return x;}
    const unsigned bytes=a.wire==s::Wire::plain?8:7;U x=0;for(unsigned b=0;b<bytes;++b)x|=U(p[bytes*i+b])<<(8*b);return x;
}
void check_controls(std::ostream& status){
    alignas(64) U in[256]{},out[256];alignas(64) std::uint8_t packed[2048],prior_bytes[1792];
    for(unsigned c=0;c<256*56+32;++c){
        if(c<256*56)in[c/56]=U{1}<<(c%56);else for(unsigned i=0;i<256;++i)in[i]=value(i+U(c)*256);
        s::prior.codec.encode(in,prior_bytes);
        U sum=0;for(auto v:in)sum+=v;
        for(const auto& a:arms()){
            std::memset(packed,0xa5,sizeof packed);a.codec.encode(in,packed);a.codec.decode(packed,out);
            require(!std::memcmp(in,out,sizeof in),"Comparator decode mismatch");require(a.codec.sum(packed)==sum,"Comparator sum mismatch");
            if(a.wire==s::Wire::planes)require(!std::memcmp(packed,prior_bytes,1792),"Plane-wire mismatch");
            for(unsigned i=0;i<256;++i){require(oracle(a,packed,i)==in[i],"Independent wire mismatch");require(a.codec.point(packed,i)==in[i],"Comparator point mismatch");}
            for(unsigned i=0;i<256;i+=16){a.codec.get16(packed,i,out);require(!std::memcmp(in+i,out,128),"Comparator get16 mismatch");}
        }
        if(c<256*56)in[c/56]=0;
    }
    status<<"Comparators checked: 14336 single bits + 32 random tiles, four arms, exact wire and all endpoints.\n";
}
struct Result {U lo=0,hi=0,state=0,addresses=0;bool operator==(const Result&)const=default;};
enum class Access {dependent,independent,get16};
const char* name(Access a){return a==Access::dependent?"dependent":a==Access::independent?"independent":"get16";}
template<Access A> __attribute__((noinline)) Result access(s::Codec codec,const std::uint8_t* p,U stride,U n,U requests,U seed){
    auto point=codec.point;auto group=codec.get16;asm volatile("":"+r"(point),"+r"(group)::"memory");
    Result r;r.state=seed;alignas(64) U out[16];
    if constexpr(A==Access::dependent){for(U j=0;j<requests;++j){const U i=r.state&(n-1),v=point(p+(i/256)*stride,unsigned(i%256));r.lo+=v;r.addresses+=i;r.state=mix(r.state+step+v);}}
    else {for(U j=0;j<requests;j+=8){std::array<U,8> indices{};for(unsigned l=0;l<8;++l){const U h=mix(seed+step*(j+l+1));indices[l]=A==Access::get16?(h&(n/16-1))*16:h&(n-1);}
        for(unsigned l=0;l<8;++l){const U i=indices[l];if constexpr(A==Access::get16){group(p+(i/256)*stride,unsigned(i%256),out);for(unsigned k=0;k<8;++k){r.lo+=out[k];r.hi+=out[k+8];}}
            else {const U v=point(p+(i/256)*stride,unsigned(i%256));if(l&1)r.hi+=v;else r.lo+=v;}r.addresses+=i;}}
        r.state=mix(seed+step*requests);
    }return r;
}
Result access(Access a,const s::Arm& arm,const Buffer& b,U n,U requests,U seed){
    if(a==Access::dependent)return access<Access::dependent>(arm.codec,b.p,arm.bytes,n,requests,seed);
    if(a==Access::independent)return access<Access::independent>(arm.codec,b.p,arm.bytes,n,requests,seed);
    return access<Access::get16>(arm.codec,b.p,arm.bytes,n,requests,seed);
}
struct Lines {std::array<unsigned,6> lines{};unsigned count=0;};
Lines required(const s::Arm& a,unsigned i,unsigned count){Lines out;
    auto byte=[&](unsigned b){unsigned line=b/64;if(std::find(out.lines.begin(),out.lines.begin()+out.count,line)==out.lines.begin()+out.count){require(out.count<out.lines.size(),"Line map overflow");out.lines[out.count++]=line;}};
    for(unsigned j=i;j<i+count;++j){if(a.wire==s::Wire::planes){byte(j);byte(256+j);byte(512+j);for(unsigned b=0;b<4;++b)byte(768+4*j+b);}
        else {unsigned width=a.wire==s::Wire::plain?8:7;for(unsigned b=0;b<width;++b)byte(width*j+b);}}
    return out;
}
struct Audit {Result result;U visits=0,unique=0,trace=0,aux_bytes=0;};
Audit replay(Access access_kind,const s::Arm& a,const Buffer& b,U n,U requests,U seed){
    Audit r;r.result.state=seed;std::vector<U> seen((b.bytes/64+63)/64);std::array<Lines,256> map{};
    unsigned grain=access_kind==Access::get16?16:1;for(unsigned i=0;i<256;i+=grain)map[i]=required(a,i,grain);
    r.aux_bytes=seen.size()*8+sizeof map;alignas(64) U actual[16];
    for(U j=0;j<requests;++j){const U h=mix(seed+step*(j+1));const U i=access_kind==Access::dependent?r.result.state&(n-1):access_kind==Access::get16?(h&(n/16-1))*16:h&(n-1);
        const auto* tile=b.p+(i/256)*a.bytes;if(grain==16)a.codec.get16(tile,unsigned(i%256),actual);else actual[0]=a.codec.point(tile,unsigned(i%256));
        for(unsigned k=0;k<grain;++k)require(actual[k]==value(i+k),"Capacity oracle mismatch");
        if(grain==16){for(unsigned k=0;k<8;++k){r.result.lo+=actual[k];r.result.hi+=actual[k+8];}}
        else if(access_kind==Access::independent && (j&1))r.result.hi+=actual[0];else r.result.lo+=actual[0];
        r.result.addresses+=i;if(access_kind==Access::dependent)r.result.state=mix(r.result.state+step+actual[0]);
        r.trace=mix(r.trace^i^(step*(j+1)));const auto& lines=map[i%256];r.visits+=lines.count;
        for(unsigned l=0;l<lines.count;++l){const U line=(i/256)*(a.bytes/64)+lines.lines[l],bit=U{1}<<(line%64);if(!(seen[line/64]&bit)){seen[line/64]|=bit;++r.unique;}}
    }
    if(access_kind!=Access::dependent)r.result.state=mix(seed+step*requests);return r;
}
struct Timing {U elapsed;long minor,major;std::string status;U cycles,instructions,misses,enabled,running;};
template<class F>Timing timed(bool pmu,int cpu,F&& f){require(sched_getcpu()==cpu,"CPU before");Perf perf(pmu);rusage before{},after{};getrusage(RUSAGE_SELF,&before);perf.start();asm volatile("":::"memory");
    const U start=now();f();const U elapsed=now()-start;asm volatile("":::"memory");perf.stop();getrusage(RUSAGE_SELF,&after);require(sched_getcpu()==cpu,"CPU after");
    return {elapsed,after.ru_minflt-before.ru_minflt,after.ru_majflt-before.ru_majflt,perf.status,perf.cycles,perf.instructions,perf.misses,perf.enabled,perf.running};}
void row(const char* suite,const char* op,const s::Arm& arm,int cpu,unsigned rep,U n,U operations,U grain,U passes,U seed,U input,U output,U allocation,const Timing& t,const Audit& a){
    std::cout<<suite<<','<<op<<','<<arm.name<<','<<cpu<<','<<rep<<','<<n<<','<<operations<<','<<grain<<','<<passes<<','<<seed<<','<<data_seed<<','<<n/256*arm.bytes<<','<<input<<','<<output<<','<<allocation<<','<<sizeof(s::Codec)<<",0,"<<a.aux_bytes<<",64,"<<sysconf(_SC_PAGESIZE)<<','
        <<t.elapsed<<','<<double(t.elapsed)/operations<<','<<double(t.elapsed)/operations/grain<<','<<a.result.lo<<','<<a.result.hi<<','<<a.result.state<<','<<a.result.addresses<<','<<a.trace<<','<<a.visits<<','<<a.unique<<','<<n/256*arm.bytes/64
        <<",unestablished,"<<t.minor<<','<<t.major<<','<<t.status<<',';
    if(t.status=="available")std::cout<<t.cycles<<','<<t.instructions<<','<<t.misses<<','<<t.enabled<<','<<t.running;else std::cout<<"NA,NA,NA,NA,NA";
    std::cout<<'\n'<<std::flush;
}
enum class Bulk {decode,encode,sum};
__attribute__((noinline)) U bulk_loop(Bulk op,s::Codec codec,std::uint8_t* packed,U* plain,U stride,U tiles,U passes){
    auto decode=codec.decode;auto encode=codec.encode;auto sum=codec.sum;
    asm volatile("":"+r"(decode),"+r"(encode),"+r"(sum)::"memory");U result=0;
    if(op==Bulk::decode){for(U p=0;p<passes;++p)for(U i=0;i<tiles;++i)decode(packed+i*stride,plain+i*256);}
    else if(op==Bulk::encode){for(U p=0;p<passes;++p)for(U i=0;i<tiles;++i)encode(plain+i*256,packed+i*stride);}
    else {for(U p=0;p<passes;++p)for(U i=0;i<tiles;++i)result+=sum(packed+i*stride);}return result;
}
U calibrate(U n){Buffer input(8*n),output(8*n);for(U i=0;i<n;++i)reinterpret_cast<U*>(input.p)[i]=value(i);U passes=1;
    for(;;){U start=now();bulk_loop(Bulk::decode,s::plain.codec,input.p,reinterpret_cast<U*>(output.p),2048,n/256,passes);if(now()-start>=20000000||passes>=(1<<20))break;passes*=2;}
    require(!std::memcmp(input.p,output.p,n*8),"Calibration output");return passes;
}
void bulk(const Options& o,int cpu,const s::Arm& a,U passes){const U n=o.bulk_values;
    for(auto op:{Bulk::decode,Bulk::encode,Bulk::sum}){Buffer packed(n/256*a.bytes);fill(a,packed,n);std::unique_ptr<Buffer> plain;
        if(op!=Bulk::sum){plain=std::make_unique<Buffer>(n*8);for(U i=0;i<n;++i)reinterpret_cast<U*>(plain->p)[i]=value(i);}
        U* values=plain?reinterpret_cast<U*>(plain->p):nullptr;bulk_loop(op,a.codec,packed.p,values,a.bytes,n/256,1);
        for(unsigned rep=0;rep<o.reps;++rep){U result=0;auto t=timed(o.pmu,cpu,[&]{result=bulk_loop(op,a.codec,packed.p,values,a.bytes,n/256,passes);});U expected=0;
            for(U i=0;i<n;++i){const U v=value(i);expected+=v;if(op==Bulk::decode)require(values[i]==v,"Bulk decode oracle");else if(op==Bulk::encode)require(oracle(a,packed.p+(i/256)*a.bytes,unsigned(i%256))==v,"Bulk encode oracle");}
            if(op==Bulk::sum)require(result==expected*passes,"Bulk sum oracle");Audit audit;audit.result.lo=op==Bulk::sum?result:expected;
            audit.unique=packed.bytes/64;audit.visits=audit.unique*passes;
            row("bulk",op==Bulk::decode?"decode":op==Bulk::encode?"encode":"sum",a,cpu,rep,n,n/256*passes,256,passes,data_seed,
                op==Bulk::encode?n*8:packed.bytes,op==Bulk::decode?n*8:op==Bulk::encode?packed.bytes:0,packed.bytes+(plain?n*8:0),t,audit);
        }
    }
}
void capacity(const Options& o,int cpu,const s::Arm& a,unsigned power){const U n=U{1}<<power;Buffer packed(n/256*a.bytes);fill(a,packed,n);
    for(auto kind:{Access::dependent,Access::independent,Access::get16})for(unsigned rep=0;rep<o.reps;++rep){const U seed=mix(data_seed^(U(power)<<40)^(U(kind)<<32)^(step*(rep+1)));Result result;
        auto t=timed(o.pmu,cpu,[&]{result=access(kind,a,packed,n,o.samples,seed);});auto audit=replay(kind,a,packed,n,o.samples,seed);require(result==audit.result,"Timed result/trace mismatch");
        row("capacity",name(kind),a,cpu,rep,n,o.samples,kind==Access::get16?16:1,1,seed,packed.bytes,0,packed.bytes,t,audit);
    }
}
}
int main(int argc,char** argv){try{auto o=options(argc,argv);int cpu=pin();check_controls(o.check_only?std::cout:std::cerr);if(o.check_only)return 0;
    std::cout<<std::setprecision(12)<<"suite,operation,arm,cpu,repetition,logical_values,operations,values_per_operation,passes,seed,data_seed,payload_bytes,input_bytes,output_bytes,allocation_bytes,hot_metadata_bytes,trace_bytes,audit_aux_bytes,payload_alignment,page_bytes,elapsed_ns,ns_per_operation,ns_per_value,checksum0,checksum1,final_state,address_sum,trace_hash,required_line_visits,required_unique_payload_lines,payload_lines,residence,minor_faults,major_faults,pmu_status,cycles_raw,instructions_raw,cache_misses_raw,pmu_time_enabled_ns,pmu_time_running_ns\n";
    U passes=o.suite=="capacity"?0:calibrate(o.bulk_values);std::cerr<<"Common bulk passes="<<passes<<", values="<<o.bulk_values<<"\n";
    for(const auto& a:arms()){if(o.suite!="capacity"){std::cerr<<a.name<<" bulk\n";bulk(o,cpu,a,passes);}if(o.suite!="bulk")for(auto power:o.powers){std::cerr<<a.name<<" capacity 2^"<<power<<"\n";capacity(o,cpu,a,power);}}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
