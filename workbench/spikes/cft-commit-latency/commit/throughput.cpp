// Bounded throughput experiment; shares the independently checked record format.
#define main latency_experiment_main
#include "bench.cpp"
#undef main
#include <random>
#include <functional>
#include <sys/resource.h>
#include <sys/mman.h>
#include <atomic>
#include "preparation.hpp"

namespace sweep {
constexpr size_t record_bytes=4096;
size_t packet_bytes=8973;
constexpr uint64_t timeout_ns=30000000000ULL;
bool configured(const char* key){const char* v=getenv(key);return v && std::string(v)!="0";}
uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
int pipeline_socket(bool udp){int fd=socket_for(udp);int size=32*1024*1024;require(!setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size)) && !setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size)),"pipeline socket buffers");return fd;}
struct Queue {
    io_uring q{};
    Queue(){int r=io_uring_queue_init(4096,&q,0);if(r<0)errno=-r;require(!r,"pipeline ring");}
    ~Queue(){io_uring_queue_exit(&q);}
    void submit(){while(io_uring_sq_ready(&q))require(io_uring_submit(&q)>0,"pipeline submit");}
    io_uring_sqe* get(uint64_t tag){auto* s=io_uring_get_sqe(&q);if(!s){submit();s=io_uring_get_sqe(&q);}require(s,"pipeline SQE");s->user_data=tag;return s;}
    bool poll(uint64_t& tag,int& result,int wait_us,bool allow_closed_peer=false){
        io_uring_cqe* c=nullptr;int r=io_uring_peek_cqe(&q,&c);
        if(r==-EAGAIN && wait_us){__kernel_timespec ts{wait_us/1000000,(wait_us%1000000)*1000L};r=io_uring_wait_cqe_timeout(&q,&c,&ts);}
        if(r==-EAGAIN || r==-ETIME)return false;
        if(r<0)errno=-r;require(r==0,"pipeline completion");tag=c->user_data;result=c->res;io_uring_cqe_seen(&q,c);
        if(allow_closed_peer && result==-ECONNREFUSED)return true;
        if(result<0)errno=-result;require(result>=0,"pipeline I/O tag="+std::to_string(tag));return true;
    }
};
struct File {
    int fd=-1;uint64_t base=0;std::string path,layout;size_t count;uint64_t run;
    Preparation preparation;
    File(const std::string& p,const std::string& l,size_t n,uint64_t r):path(p),layout(l),count(n),run(r),preparation(p,n*4096,l=="ahead"){
        require(n>0 && n<=(configured("CFT_PROBE")?16000000:2000000) && (l=="initialized" || l=="raw" || l=="ahead"),"bounded pipeline log");
        bool raw=l=="raw";base=raw ? 1024ULL*1024*1024 : 0;
        if(raw){struct stat st{};require(stat(p.c_str(),&st)==0 && S_ISBLK(st.st_mode),"pipeline raw block device");}
        int setup=open(p.c_str(),O_RDWR|(raw?0:O_CREAT|O_TRUNC),0600);require(setup>=0,"pipeline setup open");
        if(raw){uint64_t cap=0;int sector=0;require(ioctl(setup,BLKGETSIZE64,&cap)==0 && ioctl(setup,BLKSSZGET,&sector)==0 && record_bytes%sector==0 && base+n*record_bytes<=cap,"pipeline raw capacity");}
        else {int e=posix_fallocate(setup,0,preparation.initial);if(e)errno=e;require(!e,"pipeline fallocate");}
        void* mem=nullptr;require(!posix_memalign(&mem,4096,1024*1024),"pipeline setup buffer");memset(mem,0,1024*1024);
        if(!(raw && configured("CFT_SKIP_INITIALIZE")))for(size_t off=0;off<preparation.initial;){size_t amount=std::min<size_t>(1024*1024,preparation.initial-off);require(pwrite(setup,mem,amount,base+off)==static_cast<ssize_t>(amount),"pipeline initialize");off+=amount;}
        free(mem);require(fdatasync(setup)==0 && close(setup)==0,"pipeline setup sync");
        if(!raw){int dir=open(std::filesystem::path(p).parent_path().c_str(),O_RDONLY|O_DIRECTORY);require(dir>=0 && fsync(dir)==0,"pipeline directory sync");close(dir);}
        fd=open(p.c_str(),O_RDWR|O_DIRECT|O_DSYNC);require(fd>=0,"pipeline durable open");
    }
    ~File(){if(fd>=0)close(fd);}
};
void verify(const std::string& path,const std::string& layout,size_t count,uint64_t run){
    int fd=open(path.c_str(),O_RDONLY|O_DIRECT);require(fd>=0,"pipeline recovery open");
    void* actual=nullptr;void* expected=nullptr;require(!posix_memalign(&actual,4096,1024*1024) && !posix_memalign(&expected,4096,4096),"pipeline recovery buffers");
    uint64_t base=layout=="raw"?1024ULL*1024*1024:0;
    for(size_t first=0;first<count;){size_t n=std::min<size_t>(256,count-first);require(pread(fd,actual,n*4096,base+first*4096)==static_cast<ssize_t>(n*4096),"pipeline recovered range");
        for(size_t j=0;j<n;++j){record(expected,4096,run,first+j+1);require(!memcmp(static_cast<char*>(actual)+j*4096,expected,4096),"pipeline recovered identity");}first+=n;}
    close(fd);free(actual);free(expected);
}
struct Frame {uint64_t magic,run,batch,first,checksum;uint32_t bytes,records;uint16_t part,parts;uint32_t reserved=0;};
static_assert(sizeof(Frame)==56);
size_t chunk=packet_bytes-sizeof(Frame);
struct Slot {
    uint64_t batch=0,first=0,checksum=0,submitted=0;
    std::array<uint64_t,2> last_send{};
    size_t records=0,bytes=0,parts=0,received=0,pending=0;
    bool written=false,needs_write=false;
    void* data=nullptr;
    std::vector<char> stream;
    std::vector<std::vector<char>> packets;
    std::vector<bool> seen;
    Slot(size_t b){require(!posix_memalign(&data,4096,b*4096),"pipeline slot memory");}
    ~Slot(){free(data);}
    Slot(const Slot&)=delete;Slot& operator=(const Slot&)=delete;
};
uint64_t tag(int kind,size_t index=0,int peer=0){return kind|(static_cast<uint64_t>(peer)<<8)|(index<<16);}
struct Usage {rusage r{};Usage(){getrusage(RUSAGE_SELF,&r);}double seconds()const{return r.ru_utime.tv_sec+r.ru_utime.tv_usec*1e-6+r.ru_stime.tv_sec+r.ru_stime.tv_usec*1e-6;}};
std::vector<std::unique_ptr<Slot>> slots(size_t window,size_t batch){std::vector<std::unique_ptr<Slot>> out;for(size_t i=0;i<window;++i)out.push_back(std::make_unique<Slot>(batch));return out;}

void follower(bool udp,const std::string& ip,int port,size_t count,size_t window,size_t batch,File& file,uint64_t run,const std::string& output){
    file.preparation.output_path=output;
    int listener=pipeline_socket(udp);auto local=address(ip,port);require(!bind(listener,reinterpret_cast<sockaddr*>(&local),sizeof(local)),"pipeline bind");
    if(!udp)require(!listen(listener,1),"pipeline listen");std::ofstream(output+".ready")<<"ready\n";
    int fd=udp?listener:accept(listener,nullptr,nullptr);require(fd>=0,"pipeline accept");
    auto all=slots(window,batch);std::vector<uint64_t> checksums(count+1,0);
    uint64_t prefix=0,records=0,last_progress=now(),duplicates=0,drops=0,received_packets=0;
    size_t drop_every=getenv("CFT_DROP_FRAGMENT_EVERY")?std::stoul(getenv("CFT_DROP_FRAGMENT_EVERY")):0;
    size_t drop_ack=getenv("CFT_DROP_ACK_EVERY")?std::stoul(getenv("CFT_DROP_ACK_EVERY")):0;
    Usage begin;uint64_t started=now();
    {
        std::vector<char> input(packet_bytes);Frame header{};size_t recv_done=0;bool body=false,recv_pending=false,ack_pending=false,need_ack=false;
        Ack ack{};size_t ack_done=0;uint64_t ack_prefix=0,acks=0,last_ack=0;bool connected=!udp,all_received=false;
        sockaddr_in peer{};iovec iv{input.data(),input.size()};msghdr msg{};msg.msg_name=&peer;msg.msg_namelen=sizeof(peer);msg.msg_iov=&iv;msg.msg_iovlen=1;
        Queue q;
        auto receive=[&]{auto* s=q.get(tag(2));
            if(udp){msg.msg_namelen=sizeof(peer);msg.msg_flags=0;io_uring_prep_recvmsg(s,fd,&msg,0);}
            else if(!body)io_uring_prep_recv(s,fd,reinterpret_cast<char*>(&header)+recv_done,sizeof(header)-recv_done,0);
            else {auto& slot=*all[header.batch%window];io_uring_prep_recv(s,fd,static_cast<char*>(slot.data)+recv_done,header.bytes-recv_done,0);}
            recv_pending=true;};
        auto send_ack=[&]{
            if(drop_ack && ++acks%drop_ack==0){++drops;need_ack=false;return;}
            ack={magic,run,prefix,0,records};ack_done=0;ack_prefix=prefix;
            io_uring_prep_send(q.get(tag(3)),fd,&ack,sizeof(ack),MSG_NOSIGNAL);ack_pending=true;need_ack=false;last_ack=now();};
        auto shape=[&](const Frame& h){require(h.magic==magic && h.run==run && h.batch>=1 && h.batch<=count && h.first>=1 && h.records>=1 && h.records<=batch && h.bytes==h.records*4096 && h.first+h.records-1<=count,"pipeline frame shape");};
        auto prepare=[&](const Frame& h)->Slot&{
            require(h.batch<=prefix+window,"bounded follower lag");auto& s=*all[h.batch%window];
            if(s.batch!=h.batch){require(!s.batch || s.batch<=prefix,"slot past durable prefix");s.batch=h.batch;s.first=h.first;s.records=h.records;s.bytes=h.bytes;s.checksum=h.checksum;s.written=false;s.received=0;s.parts=h.parts;s.seen.assign(h.parts,false);}
            require(s.first==h.first && s.records==h.records && s.checksum==h.checksum,"duplicate batch identity");return s;};
        auto write=[&](Slot& s){require(hash(s.data,s.bytes)==s.checksum,"pipeline payload checksum");checksums[s.batch]=s.checksum;
            if(file.preparation.enabled)s.needs_write=true;
            else io_uring_prep_write(q.get(tag(1,s.batch%window)),file.fd,s.data,s.bytes,file.base+(s.first-1)*4096);};
        receive();
        uint64_t finished=0,debug_at=now();
        while(true){
            if(file.preparation.enabled)for(auto& slot:all){auto& s=*slot;if(s.needs_write && file.preparation.ready((s.first-1)*4096+s.bytes)){s.needs_write=false;s.submitted=now();file.preparation.use((s.first-1)*4096+s.bytes);io_uring_prep_write(q.get(tag(1,s.batch%window)),file.fd,s.data,s.bytes,file.base+(s.first-1)*4096);}}
            if(getenv("CFT_DEBUG") && now()-debug_at>1000000000ULL){debug_at=now();std::cerr<<"follower prefix "<<prefix<<" records "<<records<<" ack "<<ack_pending<<" receive "<<recv_pending<<" duplicates "<<duplicates<<"\n";}
            if(records==count && !ack_pending && !need_ack){if(!udp)break;if(!finished)finished=now();if(now()-finished>500000000ULL)break;}
            if(!recv_pending && ((!all_received && records<count) || udp))receive();
            if(need_ack && !ack_pending && (prefix>ack_prefix || now()-last_ack>=200000ULL))send_ack();q.submit();
            uint64_t t;int result;if(!q.poll(t,result,1000,udp && records==count)){require(now()-last_progress<(records==0?180000000000ULL:timeout_ns),"pipeline follower deadline");continue;}
            int kind=t&255;size_t index=t>>16;
            if(result==-ECONNREFUSED && udp && records==count){if(kind==2)recv_pending=false;else if(kind==3)ack_pending=false;else require(false,"unexpected closed peer completion");need_ack=false;continue;}
            if(kind==1){auto& s=*all[index];require(result==static_cast<int>(s.bytes),"pipeline full durable batch");s.written=true;
                file.preparation.pressure(now()-s.submitted);
                while(prefix<count){auto& next=*all[(prefix+1)%window];if(next.batch!=prefix+1 || !next.written)break;require(next.first==records+1,"contiguous durable record prefix");++prefix;records+=next.records;}
                need_ack=true;last_progress=now();
            }else if(kind==3){require(result>0,"pipeline ACK send");ack_done+=result;
                if(udp)require(ack_done==sizeof(ack),"whole datagram ACK");
                if(ack_done<sizeof(ack))io_uring_prep_send(q.get(tag(3)),fd,reinterpret_cast<char*>(&ack)+ack_done,sizeof(ack)-ack_done,MSG_NOSIGNAL);
                else {ack_pending=false;if(prefix>ack_prefix)need_ack=true;}
            }else{
                require(kind==2 && result>0,"pipeline receive");recv_pending=false;
                file.preparation.start();
                if(udp){require(!(msg.msg_flags&MSG_TRUNC) && result>=static_cast<int>(sizeof(Frame)),"pipeline full UDP packet");Frame h;memcpy(&h,input.data(),sizeof(h));
                    if(h.magic!=magic || h.run!=run)continue;shape(h);
                    if(!connected){require(!connect(fd,reinterpret_cast<sockaddr*>(&peer),sizeof(peer)),"pipeline UDP peer");connected=true;}
                    if(drop_every && ++received_packets%drop_every==0){++drops;continue;}
                    if(h.batch<=prefix){require(checksums[h.batch]==h.checksum,"durable duplicate checksum");++duplicates;need_ack=true;continue;}
                    require(h.parts==(h.bytes+chunk-1)/chunk && h.part<h.parts,"pipeline fragments");auto& s=prepare(h);
                    size_t amount=std::min(chunk,s.bytes-h.part*chunk);require(result==static_cast<int>(sizeof(Frame)+amount),"pipeline fragment size");
                    if(!s.seen[h.part]){memcpy(static_cast<char*>(s.data)+h.part*chunk,input.data()+sizeof(Frame),amount);s.seen[h.part]=true;if(++s.received==s.parts)write(s);}else ++duplicates;
                }else if(!body){recv_done+=result;if(recv_done==sizeof(header)){shape(header);require(header.batch==prefix+1 || header.batch>prefix,"TCP future batch");prepare(header);body=true;recv_done=0;}}
                else {recv_done+=result;if(recv_done==header.bytes){write(*all[header.batch%window]);all_received=header.first+header.records-1==count;body=false;recv_done=0;}}
            }
        }
        // Destroy the ring (cancelling a lingering receive) before its buffers.
        if(recv_pending){io_uring_prep_cancel64(q.get(tag(9)),tag(2),0);q.submit();io_uring_cqe* c=nullptr;for(int i=0;i<2;++i){require(!io_uring_wait_cqe(&q.q,&c),"cancel receive CQE");io_uring_cqe_seen(&q.q,c);}}
    }
    Usage end;close(fd);if(fd!=listener)close(listener);
    file.preparation.finish(output);
    std::cout<<"{\"durable_records\":"<<records<<",\"duplicates\":"<<duplicates<<",\"injected_drops\":"<<drops<<",\"traffic_seconds\":"<<(now()-started)*1e-9<<",\"process_cpu_seconds\":"<<end.seconds()-begin.seconds()<<"}\n";
}

struct Peer {int fd=-1;Ack ack{};size_t ack_bytes=0;uint64_t prefix=0,next_send=1,sending=0;size_t sent=0;};
struct Sample {uint64_t arrival=0,submit=0,commit=0,batch=0;uint32_t batch_records=0;};
struct Detail {uint64_t prepare=0,local=0,ack0=0,ack1=0;};
struct Observation {
    uint64_t elapsed,issued,committed,local,peer0,peer1,released,queued,oldest_ns,loop_gap_ns;
    uint32_t batch_cap,rtt0,rtt1,retrans0,retrans1,unacked0,unacked1;
    uint64_t prepared_bytes=0,reserve_bytes=0;
};
void leader(bool udp,const std::array<std::string,2>& ips,int port,size_t count,size_t window,size_t batch,double rate,uint64_t batch_wait,File& file,uint64_t run,uint64_t seed,const std::string& output,bool local_only=false){
    file.preparation.output_path=output;
    std::array<Peer,2> peers;
    if(!local_only)for(int p=0;p<2;++p){peers[p].fd=pipeline_socket(udp);auto dest=address(ips[p],port);require(!connect(peers[p].fd,reinterpret_cast<sockaddr*>(&dest),sizeof(dest)),"pipeline connect");}
    auto all=slots(window,batch);std::vector<Sample> samples(count);std::mt19937_64 rng(seed);std::exponential_distribution<double> exponential(rate?rate:1);
    bool probe=configured("CFT_PROBE"),adaptive=configured("CFT_ADAPT_BATCH");
    size_t base_batch=getenv("CFT_BASE_BATCH")?std::stoul(getenv("CFT_BASE_BATCH")):batch;
    require(base_batch>=1 && base_batch<=batch,"base batch capacity");
    std::vector<Detail> details(probe?count:0);std::vector<Observation> observations;
    size_t cap=base_batch;uint64_t last_slow=0,observe_at=0,last_loop=0,max_loop_gap=0;
    observations.reserve(100000);
    uint64_t offered_end=0;
    for(auto& s:samples){
        if(rate){
            double interval=exponential(rng)*1e9;
            if(configured("CFT_BURST")){
                // Alternate five seconds at 0.5x and 1.5x rate, preserving hazard across boundaries.
                while(interval>0){uint64_t segment=offered_end/5000000000ULL;double factor=segment%2?1.5:.5;
                    uint64_t remaining=(segment+1)*5000000000ULL-offered_end;
                    if(interval/factor<remaining){offered_end+=static_cast<uint64_t>(interval/factor);break;}
                    interval-=remaining*factor;offered_end+=remaining;
                }
            }else offered_end+=static_cast<uint64_t>(interval);
        }
        s.arrival=offered_end;
    }
    uint64_t issued=0,next=1,released=0,local=0,committed=0,committed_records=0,retries=0,max_lag=0,last_progress=now();
    uint64_t start=now()+100000000ULL;Usage begin;
    if(probe){std::ofstream anchor(output+".clock.json");anchor<<"{\"steady_start_ns\":"<<start<<",\"realtime_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()<<",\"steady_now_ns\":"<<now()<<"}\n";}
    bool successful=false;
    struct Partial {std::function<void()> save;~Partial(){try{save();}catch(...){}}};
    Partial partial{[&]{if(successful)return;uint64_t cutoff=now()>start?now()-start:0;
        size_t due=rate?std::upper_bound(samples.begin(),samples.end(),cutoff,[](uint64_t t,const Sample& s){return t<s.arrival;})-samples.begin():issued;
        std::ofstream detail(output+".partial.json");detail<<"{\"failed\":true,\"cutoff_ns\":"<<cutoff<<",\"planned\":"<<count<<",\"offered\":"<<due<<",\"admitted\":"<<issued<<",\"committed\":"<<committed_records<<",\"outstanding\":"<<due-committed_records<<",\"retried_packets\":"<<retries<<"}\n";
        std::ofstream rows(output+".partial.csv");rows<<"sequence,arrival_ns,prepared_ns,commit_ns,status\n";
        for(size_t i=0;i<count;++i){const auto& r=samples[i];rows<<i+1<<','<<r.arrival<<','<<r.submit<<','<<r.commit<<','<<(r.commit?"committed":(i<issued?"admitted":(i<due?"queued":"future")))<<'\n';}
    }};
    {
        Queue q;
        auto receive=[&](int p){auto& peer=peers[p];io_uring_prep_recv(q.get(tag(4,0,p)),peer.fd,reinterpret_cast<char*>(&peer.ack)+peer.ack_bytes,sizeof(Ack)-peer.ack_bytes,0);};
        auto send_udp=[&](Slot& s,int p){for(auto& packet:s.packets){io_uring_prep_send(q.get(tag(3,s.batch%window,p)),peers[p].fd,packet.data(),packet.size(),MSG_NOSIGNAL);++s.pending;}s.last_send[p]=now();};
        auto send_tcp=[&](int p){auto& peer=peers[p];if(!peer.sending && peer.next_send<next){peer.sending=peer.next_send;peer.sent=0;}
            if(peer.sending){auto& s=*all[peer.sending%window];require(s.batch==peer.sending,"live stream batch");io_uring_prep_send(q.get(tag(2,s.batch%window,p)),peer.fd,s.stream.data()+peer.sent,s.stream.size()-peer.sent,MSG_NOSIGNAL);++s.pending;}};
        if(!local_only)for(int p=0;p<2;++p)receive(p);
        uint64_t debug_at=now();
        while(released+1<next || issued<count){
            if(getenv("CFT_DEBUG") && now()-debug_at>1000000000ULL){debug_at=now();std::cerr<<"leader issued "<<issued<<" local "<<local<<" peers "<<peers[0].prefix<<","<<peers[1].prefix<<" released "<<released<<" next "<<next<<" retries "<<retries<<"\n";}
            require(now()-last_progress<timeout_ns,"pipeline progress deadline");
            uint64_t time=now();if(time<start){q.submit();uint64_t t;int r;require(!q.poll(t,r,1000),"no early ACK");continue;}
            uint64_t elapsed=time-start;
            file.preparation.start();
            if(last_loop)max_loop_gap=std::max(max_loop_gap,time-last_loop);last_loop=time;
            uint64_t oldest=issued<count && elapsed>samples[issued].arrival?elapsed-samples[issued].arrival:0;
            if(adaptive){if(oldest>200000){cap=batch;last_slow=elapsed;}else if(elapsed-last_slow>100000000)cap=base_batch;}
            file.preparation.pressure(oldest);
            if(probe && elapsed>=observe_at){
                size_t due=rate?std::upper_bound(samples.begin(),samples.end(),elapsed,[](uint64_t t,const Sample& s){return t<s.arrival;})-samples.begin():issued;
                Observation o{elapsed,issued,committed_records,local,peers[0].prefix,peers[1].prefix,released,due-issued,oldest,max_loop_gap,static_cast<uint32_t>(cap),0,0,0,0,0,0};
                if(file.preparation.enabled){o.prepared_bytes=file.preparation.prepared.load();o.reserve_bytes=o.prepared_bytes-issued*4096;}
                if(!udp && !local_only){tcp_info a{},b{};socklen_t length=sizeof(a);require(!getsockopt(peers[0].fd,IPPROTO_TCP,TCP_INFO,&a,&length),"TCP diagnostic");length=sizeof(b);require(!getsockopt(peers[1].fd,IPPROTO_TCP,TCP_INFO,&b,&length),"TCP diagnostic");o.rtt0=a.tcpi_rtt;o.rtt1=b.tcpi_rtt;o.retrans0=a.tcpi_total_retrans;o.retrans1=b.tcpi_total_retrans;o.unacked0=a.tcpi_unacked;o.unacked1=b.tcpi_unacked;}
                observations.push_back(o);observe_at=elapsed+10000000;max_loop_gap=0;
            }
            // Arrivals were generated independently of window availability. They queue here.
            while(issued<count && next-released<=window){
                time=now()-start;size_t available=0;
                if(rate){while(available<cap && issued+available<count && samples[issued+available].arrival<=time)++available;
                    if(!available || (available<cap && issued+available<count && time<samples[issued].arrival+batch_wait))break;
                }else available=std::min(cap,count-issued);
                if(!file.preparation.ready((issued+available)*4096))break;
                auto& s=*all[next%window];s.batch=next;s.first=issued+1;s.records=available;s.bytes=available*4096;s.written=false;s.pending=0;s.parts=(s.bytes+chunk-1)/chunk;
                for(size_t j=0;j<available;++j)record(static_cast<char*>(s.data)+j*4096,4096,run,issued+j+1);
                s.checksum=hash(s.data,s.bytes);Frame h{magic,run,next,issued+1,s.checksum,static_cast<uint32_t>(s.bytes),static_cast<uint32_t>(available),0,static_cast<uint16_t>(s.parts),0};
                if(udp){s.packets.clear();for(size_t part=0;part<s.parts;++part){size_t amount=std::min(chunk,s.bytes-part*chunk);s.packets.emplace_back(sizeof(Frame)+amount);h.part=part;memcpy(s.packets.back().data(),&h,sizeof(h));memcpy(s.packets.back().data()+sizeof(h),static_cast<char*>(s.data)+part*chunk,amount);}}
                else {s.stream.resize(sizeof(Frame)+s.bytes);memcpy(s.stream.data(),&h,sizeof(h));memcpy(s.stream.data()+sizeof(h),s.data,s.bytes);}
                s.submitted=now()-start;for(size_t j=0;j<available;++j){auto& sample=samples[issued+j];if(!rate)sample.arrival=time;sample.submit=s.submitted;sample.batch=next;sample.batch_records=available;if(probe)details[issued+j].prepare=time;}
                io_uring_prep_write(q.get(tag(1,next%window)),file.fd,s.data,s.bytes,file.base+issued*4096);
                ++next;issued+=available;
                file.preparation.use(issued*4096);
                if(!local_only){if(udp)for(int p=0;p<2;++p)send_udp(s,p);else for(int p=0;p<2;++p)if(!peers[p].sending)send_tcp(p);}
            }
            for(int p=0;p<2;++p)max_lag=std::max(max_lag,next-1-peers[p].prefix);
            if(udp){uint64_t time2=now();for(int p=0;p<2;++p){uint64_t id=peers[p].prefix+1;if(id>=next)continue;auto& s=*all[id%window];if(!s.pending && time2-s.last_send[p]>=2000000ULL){retries+=s.parts;send_udp(s,p);}}}
            q.submit();uint64_t t;int result;
            // Poll without sleeping when the next scheduled arrival or batching deadline is imminent.
            int wait=1000;if(rate && issued<count && next-released<=window){uint64_t fill=samples[std::min<uint64_t>(count-1,issued+cap-1)].arrival;uint64_t due=std::min(fill,samples[issued].arrival+batch_wait);uint64_t elapsed=now()-start;wait=due<=elapsed?0:std::min<uint64_t>(1000,(due-elapsed)/1000);}
            if(!q.poll(t,result,wait)){require(now()-last_progress<timeout_ns,"pipeline leader deadline");continue;}
            int kind=t&255,p=(t>>8)&255;size_t index=t>>16;
            if(kind==1){auto& s=*all[index];require(result==static_cast<int>(s.bytes),"leader full durable batch");s.written=true;
                if(probe){uint64_t at=now()-start;for(size_t j=0;j<s.records;++j)details[s.first-1+j].local=at;}
                while(local+1<next){auto& n=*all[(local+1)%window];if(n.batch!=local+1 || !n.written)break;++local;}
                if(local_only){last_progress=now();peers[0].prefix=peers[1].prefix=local;if(probe)for(size_t j=0;j<s.records;++j){auto& d=details[s.first-1+j];d.ack0=d.ack1=d.local;}}
            }else if(kind==2){auto& peer=peers[p];auto& s=*all[index];require(result>0 && result<=static_cast<int>(s.stream.size()-peer.sent),"pipeline TCP send bytes");--s.pending;peer.sent+=result;
                if(peer.sent==s.stream.size()){peer.next_send=peer.sending+1;peer.sending=0;}send_tcp(p);
            }else if(kind==3){auto& s=*all[index];require(result>=static_cast<int>(sizeof(Frame)) && result<=static_cast<int>(packet_bytes),"pipeline UDP send bytes");require(s.pending>0,"tracked packet send");--s.pending;
            }else{require(kind==4 && result>0,"pipeline ACK receive");auto& peer=peers[p];peer.ack_bytes+=result;
                if(udp)require(peer.ack_bytes==sizeof(Ack),"pipeline UDP ACK size");
                if(peer.ack_bytes==sizeof(Ack)){auto& a=peer.ack;require(a.magic==magic && a.run==run && a.sequence<next,"pipeline ACK identity");if(a.sequence>peer.prefix)last_progress=now();
                    if(probe){uint64_t at=now()-start;for(uint64_t id=peer.prefix+1;id<=a.sequence;++id){auto& s=*all[id%window];require(s.batch==id,"diagnostic ACK slot");for(size_t j=0;j<s.records;++j){auto& d=details[s.first-1+j];(p?d.ack1:d.ack0)=at;}}}
                    peer.prefix=std::max(peer.prefix,a.sequence);peer.ack_bytes=0;}
                if(peer.prefix+1<next || issued<count)receive(p);
            }
            uint64_t prefix=std::min(local,std::max(peers[0].prefix,peers[1].prefix));
            uint64_t finished=now()-start;
            while(committed<prefix){auto& s=*all[(++committed)%window];require(s.first==committed_records+1,"contiguous leader commit prefix");for(size_t j=0;j<s.records;++j)samples[s.first-1+j].commit=finished;committed_records+=s.records;}
            while(released+1<next){auto& s=*all[(released+1)%window];if(!s.written || s.pending || peers[0].prefix<s.batch || peers[1].prefix<s.batch)break;++released;}
        }
    }
    uint64_t end=now()-start;Usage finish;require(committed_records==count,"all offered records committed");
    file.preparation.finish(output);
    for(auto& peer:peers)if(peer.fd>=0)close(peer.fd);
    successful=true;
    if(configured("CFT_BINARY")){
        static_assert(sizeof(Sample)==40 && sizeof(Detail)==32);
        std::ofstream out(output,std::ios::binary);out.write(reinterpret_cast<const char*>(samples.data()),samples.size()*sizeof(Sample));
        if(probe){std::ofstream trace(output+".detail",std::ios::binary);trace.write(reinterpret_cast<const char*>(details.data()),details.size()*sizeof(Detail));}
    }else{std::ofstream out(output);out<<"arrival_ns,prepared_ns,commit_ns,batch,batch_records\n";
        for(const auto& s:samples){require(s.commit>=s.submit && s.submit>=s.arrival,"ordered latency timestamps");out<<s.arrival<<','<<s.submit<<','<<s.commit<<','<<s.batch<<','<<s.batch_records<<'\n';}}
    if(probe){std::ofstream o(output+".observations.csv");o<<"elapsed_ns,issued,committed,local_batch,peer0_batch,peer1_batch,released_batch,queued,oldest_ns,loop_gap_ns,batch_cap,rtt0_us,rtt1_us,retrans0,retrans1,unacked0,unacked1,prepared_bytes,reserve_bytes\n";
        for(const auto& x:observations)o<<x.elapsed<<','<<x.issued<<','<<x.committed<<','<<x.local<<','<<x.peer0<<','<<x.peer1<<','<<x.released<<','<<x.queued<<','<<x.oldest_ns<<','<<x.loop_gap_ns<<','<<x.batch_cap<<','<<x.rtt0<<','<<x.rtt1<<','<<x.retrans0<<','<<x.retrans1<<','<<x.unacked0<<','<<x.unacked1<<','<<x.prepared_bytes<<','<<x.reserve_bytes<<'\n';}
    std::cout<<"{\"offered\":"<<count<<",\"committed\":"<<committed_records<<",\"batches\":"<<next-1<<",\"offered_end_ns\":"<<offered_end<<",\"drained_ns\":"<<end<<",\"retried_packets\":"<<retries<<",\"max_follower_lag_batches\":"<<max_lag<<",\"process_cpu_seconds\":"<<finish.seconds()-begin.seconds()<<"}\n";
}
}
int main(int argc,char** argv){try{
    if(const char* configured=getenv("CFT_PACKET_BYTES")){
        sweep::packet_bytes=std::stoul(configured);
        require(sweep::packet_bytes>=1472 && sweep::packet_bytes<=8973,"pipeline UDP packet bound");
        sweep::chunk=sweep::packet_bytes-sizeof(sweep::Frame);
    }
    if(argc==6 && std::string(argv[1])=="recover"){sweep::verify(argv[2],argv[3],std::stoull(argv[4]),std::stoull(argv[5]));std::cout<<"{\"verified_records\":"<<argv[4]<<"}\n";return 0;}
    require(argc==16,"ROLE PROTO IP1 IP2 PORT COUNT WINDOW BATCH RATE WAIT_US PATH LAYOUT OUTPUT RUN SEED");
    std::string role=argv[1],protocol=argv[2];require(protocol=="tcp" || protocol=="udp","pipeline transport");bool udp=protocol=="udp";
    int port=std::stoi(argv[5]);size_t count=std::stoull(argv[6]),window=std::stoul(argv[7]),batch=std::stoul(argv[8]);double rate=std::stod(argv[9]);
    require(window>=1 && window<=64 && batch>=1 && batch<=64 && rate>=0,"pipeline settings");uint64_t run=std::stoull(argv[14]);
    sweep::File file(argv[11],argv[12],count,run);
    if(role=="follower")sweep::follower(udp,argv[3],port,count,window,batch,file,run,argv[13]);
    else{require(role=="leader" || role=="local","pipeline role");sweep::leader(udp,{argv[3],argv[4]},port,count,window,batch,rate,std::stoull(argv[10])*1000,file,run,std::stoull(argv[15]),argv[13],role=="local");}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
