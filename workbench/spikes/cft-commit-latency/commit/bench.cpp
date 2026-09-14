#include <liburing.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
constexpr uint64_t magic=0x5349584346543031ULL;
constexpr size_t warmup=64;
void require(bool ok,const std::string& what) {
    if(!ok)throw std::runtime_error(what+": "+std::strerror(errno));
}
uint64_t ns(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();
}
struct Header {
    uint64_t magic,run,sequence;
    uint32_t bytes;
    uint16_t part,parts;
    uint64_t checksum;
};
struct Ack {uint64_t magic,run,sequence,checksum,durable_ns;};
static_assert(sizeof(Header)==40 && sizeof(Ack)==40);
// This homogeneous-worker experiment uses native little-endian headers.
uint64_t hash(const void* memory,size_t bytes) {
    auto* words=static_cast<const uint64_t*>(memory);
    uint64_t value=0x9e3779b97f4a7c15ULL;
    for(size_t i=0;i<bytes/8;++i)value=(value^words[i])*0x100000001b3ULL;
    return value;
}
void record(void* memory,size_t bytes,uint64_t run,uint64_t sequence) {
    auto* words=static_cast<uint64_t*>(memory);
    for(size_t i=0;i<bytes/8;++i)words[i]=run^sequence^(0x9e3779b97f4a7c15ULL*(i+1));
}
void recover(const std::string& path,const std::string& layout,size_t bytes,size_t count,uint64_t run) {
    uint64_t base=layout=="raw" ? 1024ULL*1024*1024 : 0;
    int fd=open(path.c_str(),O_RDONLY|O_DIRECT);require(fd>=0,"open direct recovery");
    void* actual=nullptr;void* expected=nullptr;
    require(posix_memalign(&actual,4096,bytes)==0 && posix_memalign(&expected,4096,bytes)==0,"recovery buffers");
    for(size_t i=0;i<count;++i){require(pread(fd,actual,bytes,base+i*bytes)==static_cast<ssize_t>(bytes),"read recovered record");
        record(expected,bytes,run,i+1);require(memcmp(actual,expected,bytes)==0,"recovered record identity");}
    close(fd);free(actual);free(expected);
}
struct Ring {
    io_uring q{};
    Ring(){int r=io_uring_queue_init(256,&q,0);if(r<0)errno=-r;require(r==0,"ring init");}
    ~Ring(){io_uring_queue_exit(&q);}
    io_uring_sqe* sqe(uint64_t tag) {
        auto* s=io_uring_get_sqe(&q);require(s,"available SQE");s->user_data=tag;return s;
    }
    void submit(){while(io_uring_sq_ready(&q))require(io_uring_submit(&q)>0,"submit SQEs");}
    bool completion(uint64_t& tag,int& result,int timeout_us=10000000) {
        __kernel_timespec timeout{timeout_us/1000000,(timeout_us%1000000)*1000L};
        io_uring_cqe* c=nullptr;int r=io_uring_wait_cqe_timeout(&q,&c,&timeout);
        if(r==-ETIME)return false;
        if(r<0)errno=-r;require(r==0,"wait completion");
        tag=c->user_data;result=c->res;io_uring_cqe_seen(&q,c);
        if(result<0)errno=-result;
        require(result>=0,"successful completion tag="+std::to_string(tag));return true;
    }
    int one(){submit();uint64_t tag;int result;require(completion(tag,result),"I/O deadline");return result;}
};
struct Log {
    std::string path,method,layout;
    int fd=-1;
    size_t bytes,count;
    uint64_t run,base=0;
    void* memory=nullptr;
    bool dsync;
    Log(std::string p,std::string m,std::string l,size_t b,size_t n,uint64_t cookie)
        :path(p),method(m),layout(l),bytes(b),count(n),run(cookie),dsync(m=="direct-dsync") {
        require(m=="direct-dsync" || m=="direct-fdatasync" || m=="buffered-fdatasync","durable method");
        require(l=="raw" || l=="initialized" || l=="extend","log layout");
        require(b>=512 && b<=65536 && b%512==0 && n>0 && n<=200000 && n*b<=2ULL*1024*1024*1024,"bounded log");
        require(posix_memalign(&memory,4096,std::max<size_t>(b,1024*1024))==0,"aligned log buffer");
        struct stat st{};
        bool raw=l=="raw";
        if(raw){require(stat(p.c_str(),&st)==0 && S_ISBLK(st.st_mode),"raw log device");base=1024ULL*1024*1024;}
        int setup=open(p.c_str(),O_RDWR|(raw ? 0 : O_CREAT|O_TRUNC),0600);require(setup>=0,"open log setup");
        if(raw){uint64_t capacity=0;int sector=0;
            require(ioctl(setup,BLKGETSIZE64,&capacity)==0 && ioctl(setup,BLKSSZGET,&sector)==0,"log geometry");
            require(bytes%sector==0 && base<=capacity && n*b<=capacity-base,"log device range");
        } else if(l=="initialized") {
            int r=posix_fallocate(setup,0,n*b);if(r)errno=r;require(r==0,"log allocation");
        }
        if(l!="extend") {
            record(memory,1024*1024,run,0);
            for(uint64_t off=0;off<n*b;) {
                size_t size=std::min<uint64_t>(1024*1024,n*b-off);
                require(pwrite(setup,memory,size,base+off)==static_cast<ssize_t>(size),"initialize log");off+=size;
            }
        }
        require(fdatasync(setup)==0 && close(setup)==0,"persist log setup");
        if(!raw){int parent=open(std::filesystem::path(p).parent_path().c_str(),O_RDONLY|O_DIRECTORY);
            require(parent>=0 && fsync(parent)==0,"persist log directory");close(parent);}
        fd=open(p.c_str(),O_RDWR|(m=="buffered-fdatasync" ? 0 : O_DIRECT)|(dsync ? O_DSYNC : 0));
        require(fd>=0,"open durable log");
    }
    ~Log(){if(fd>=0)close(fd);free(memory);}
    void prepare(Ring& ring,uint64_t seq) {
        auto* s=ring.sqe(1);io_uring_prep_write(s,fd,memory,bytes,base+(seq-1)*bytes);
        if(!dsync){s->flags|=IOSQE_IO_LINK;io_uring_prep_fsync(ring.sqe(2),fd,IORING_FSYNC_DATASYNC);}
    }
    uint64_t persist(Ring& ring,uint64_t seq) {
        auto start=Clock::now();prepare(ring,seq);ring.submit();bool write=false,sync=dsync;
        for(int i=0;i<(dsync ? 1 : 2);++i){uint64_t tag;int result;require(ring.completion(tag,result),"durability deadline");
            if(tag==1){require(result==static_cast<int>(bytes),"full durable log write");write=true;}
            else{require(tag==2 && result==0,"datasync completion");sync=true;}}
        require(write && sync,"complete durability chain");return ns(start,Clock::now());
    }
    void verify() {
        require(close(fd)==0,"close written log");fd=-1;
        recover(path,layout,bytes,count,run);
    }
};
sockaddr_in address(const std::string& ip,int port) {
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);
    require(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)==1,"IPv4 address");return a;
}
int socket_for(bool udp) {
    int fd=socket(AF_INET,udp ? SOCK_DGRAM : SOCK_STREAM,0);require(fd>=0,"socket");
    int one=1,capacity=4*1024*1024;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&capacity,sizeof(capacity));
    setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&capacity,sizeof(capacity));
    if(udp){int df=IP_PMTUDISC_DO;require(setsockopt(fd,IPPROTO_IP,IP_MTU_DISCOVER,&df,sizeof(df))==0,"UDP DF");}
    else require(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one))==0,"TCP_NODELAY");
    return fd;
}
void transfer(Ring& ring,int fd,void* memory,size_t bytes,bool send) {
    size_t done=0;
    while(done<bytes){auto* s=ring.sqe(10);
        if(send)io_uring_prep_send(s,fd,static_cast<char*>(memory)+done,bytes-done,MSG_NOSIGNAL);
        else io_uring_prep_recv(s,fd,static_cast<char*>(memory)+done,bytes-done,0);
        int r=ring.one();require(r>0 && static_cast<size_t>(r)<=bytes-done,"full stream transfer");done+=r;}
}

void follower(bool udp,const std::string& ip,int port,size_t bytes,size_t count,Log& log,uint64_t run,size_t packet,const std::string& output) {
    int listener=socket_for(udp);auto local=address(ip=="-" ? "0.0.0.0" : ip,port);
    require(bind(listener,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0,"bind follower");
    int fd=listener;
    if(!udp)require(listen(listener,1)==0,"listen");
    std::ofstream(output+".ready")<<"ready\n";
    if(!udp){fd=accept(listener,nullptr,nullptr);require(fd>=0,"accept");
        int one=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));}
    uint64_t durable=0,durable_ns=0,last_checksum=0,duplicates=0;
    std::vector<char> input(bytes+sizeof(Header));
    size_t chunk=packet-sizeof(Header),parts=(bytes+chunk-1)/chunk;
    std::vector<bool> received(parts,false);size_t received_count=0;
    std::vector<uint64_t> durable_checksums(count+1,0);
    // Deterministic loss injection is used by the local protocol check only.
    auto env_number=[](const char* key){auto* value=getenv(key);return value ? std::stoul(value) : 0UL;};
    size_t drop_fragment=env_number("CFT_DROP_FRAGMENT_EVERY"),drop_ack=env_number("CFT_DROP_ACK_EVERY");
    uint64_t packets_seen=0,injected_drops=0;
    Ring ring;
    auto finished=Clock::time_point::max();
    while(true) {
        Header h{};sockaddr_in peer{};
        if(udp){
            if(durable==count){pollfd p{fd,POLLIN,0};int ready=poll(&p,1,1000);require(ready>=0,"linger poll");
                if(!ready || ns(finished,Clock::now())>3000000000ULL)break;}
            iovec vector{input.data(),input.size()};msghdr message{};
            message.msg_name=&peer;message.msg_namelen=sizeof(peer);message.msg_iov=&vector;message.msg_iovlen=1;
            io_uring_prep_recvmsg(ring.sqe(10),fd,&message,0);ring.submit();uint64_t tag;int result;
            require(ring.completion(tag,result,durable==0 ? 180000000 : 10000000),"follower receive deadline");
            require(!(message.msg_flags&MSG_TRUNC) && result>=static_cast<int>(sizeof(Header)),"complete UDP fragment");
            memcpy(&h,input.data(),sizeof(h));
            if(h.magic!=magic || h.run!=run)continue;
            if(drop_fragment && ++packets_seen%drop_fragment==0){++injected_drops;continue;}
            require(h.bytes==bytes && h.parts==parts && h.part<parts && h.sequence>=1 && h.sequence<=count,"fragment shape");
            if(h.sequence<=durable){
                require(h.checksum==durable_checksums[h.sequence],"duplicate record matches durable identity");
                ++duplicates;Ack ack{magic,run,h.sequence,durable_checksums[h.sequence],durable_ns};
                iovec av{&ack,sizeof(ack)};message.msg_iov=&av;message.msg_namelen=sizeof(peer);
                io_uring_prep_sendmsg(ring.sqe(11),fd,&message,MSG_NOSIGNAL);
                require(ring.one()==sizeof(ack),"duplicate ACK");continue;
            }
            require(h.sequence==durable+1 && h.sequence<=count,"contiguous UDP log");
            size_t amount=std::min(chunk,bytes-h.part*chunk);
            require(result==static_cast<int>(sizeof(Header)+amount),"fragment length");
            if(!received[h.part]){memcpy(static_cast<char*>(log.memory)+h.part*chunk,input.data()+sizeof(Header),amount);
                received[h.part]=true;++received_count;}
            if(received_count!=parts)continue;
            require(hash(log.memory,bytes)==h.checksum,"assembled checksum");
        } else {
            transfer(ring,fd,input.data(),input.size(),false);memcpy(&h,input.data(),sizeof(h));
            require(h.magic==magic && h.run==run && h.sequence==durable+1 && h.bytes==bytes,"contiguous TCP log");
            memcpy(log.memory,input.data()+sizeof(Header),bytes);require(hash(log.memory,bytes)==h.checksum,"TCP checksum");
        }
        durable_ns=log.persist(ring,h.sequence);durable=h.sequence;last_checksum=h.checksum;durable_checksums[durable]=h.checksum;
        Ack ack{magic,run,durable,last_checksum,durable_ns};
        if(udp){
            if(drop_ack && durable%drop_ack==0)++injected_drops;
            else{iovec vector{&ack,sizeof(ack)};msghdr message{};message.msg_name=&peer;message.msg_namelen=sizeof(peer);
                message.msg_iov=&vector;message.msg_iovlen=1;io_uring_prep_sendmsg(ring.sqe(11),fd,&message,MSG_NOSIGNAL);
                require(ring.one()==sizeof(ack),"durable UDP ACK");}
            std::fill(received.begin(),received.end(),false);received_count=0;
        } else transfer(ring,fd,&ack,sizeof(ack),true);
        if(durable==count){if(!udp)break;finished=Clock::now();}
        if(finished!=Clock::time_point::max() && ns(finished,Clock::now())>3000000000ULL)break;
    }
    require(durable==count,"complete follower prefix");close(fd);if(fd!=listener)close(listener);
    log.verify();std::cout<<"{\"verified_records\":"<<count<<",\"duplicates\":"<<duplicates<<",\"injected_drops\":"<<injected_drops<<"}\n";
}

struct Peer {
    int fd=-1;
    Ack ack{};
    size_t received=0,sent=0,pending=0;
    bool durable=false;
    Clock::time_point last_send;
};
struct Timing {uint64_t commit,all,local,remote1,remote2,retries;};

void leader(bool udp,const std::vector<std::string>& ips,int port,size_t bytes,size_t count,
            Log& log,uint64_t run,size_t packet,int retry_us,const std::string& output) {
    std::vector<Peer> peers;
    for(const auto& ip:ips)if(ip!="-"){
        Peer p;p.fd=socket_for(udp);auto dest=address(ip,port);
        require(connect(p.fd,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==0,"connect follower");
        if(udp){int mtu=0;socklen_t n=sizeof(mtu);
            require(getsockopt(p.fd,IPPROTO_IP,IP_MTU,&mtu,&n)==0 && packet+28<=static_cast<size_t>(mtu),"UDP packet fits path MTU");}
        peers.push_back(p);
    }
    require(peers.size()>=1 && peers.size()<=2,"three-voter surviving remote quorum");
    size_t chunk=udp ? packet-sizeof(Header) : bytes,parts=(bytes+chunk-1)/chunk;
    require(parts<=128,"bounded fragment count");
    std::vector<std::vector<char>> packets(parts);
    for(size_t j=0;j<parts;++j)packets[j].resize(sizeof(Header)+std::min(chunk,bytes-j*chunk));
    std::vector<Timing> timings;timings.reserve(count-warmup);
    Ring ring;
    auto receive=[&](size_t i){auto& p=peers[i];
        io_uring_prep_recv(ring.sqe(0x20000+i),p.fd,reinterpret_cast<char*>(&p.ack)+p.received,sizeof(Ack)-p.received,0);};
    auto send=[&](size_t i){auto& p=peers[i];
        for(size_t j=0;j<parts;++j){auto& data=packets[j];size_t offset=udp ? 0 : p.sent;
            io_uring_prep_send(ring.sqe(0x10000+i*256+j),p.fd,data.data()+offset,data.size()-offset,MSG_NOSIGNAL);++p.pending;}
        p.last_send=Clock::now();};
    for(uint64_t seq=1;seq<=count;++seq) {
        record(log.memory,bytes,run,seq);uint64_t checksum=hash(log.memory,bytes);
        for(size_t j=0;j<parts;++j){Header h{magic,run,seq,static_cast<uint32_t>(bytes),static_cast<uint16_t>(j),static_cast<uint16_t>(parts),checksum};
            memcpy(packets[j].data(),&h,sizeof(h));memcpy(packets[j].data()+sizeof(h),static_cast<char*>(log.memory)+j*chunk,packets[j].size()-sizeof(h));}
        for(auto& p:peers){p.ack={};p.received=0;p.sent=0;p.pending=0;p.durable=false;}
        bool written=false,synced=log.dsync;uint64_t committed=0,local=0,retries=0;
        auto start=Clock::now();
        log.prepare(ring,seq);
        for(size_t i=0;i<peers.size();++i){receive(i);send(i);}
        ring.submit();
        while(true) {
            uint64_t tag;int result;
            if(ring.completion(tag,result,udp ? retry_us : 1000000)) {
                if(tag==1){require(result==static_cast<int>(bytes),"leader complete log write");written=true;}
                else if(tag==2){require(result==0,"leader datasync");synced=true;}
                else if(tag>=0x20000){
                    size_t i=tag-0x20000;require(i<peers.size(),"ACK peer identity");auto& p=peers[i];
                    require(result>0,"follower connection remains open");
                    if(udp){require(result==sizeof(Ack),"complete UDP ACK");p.received=sizeof(Ack);}
                    else{p.received+=result;require(p.received<=sizeof(Ack),"ACK stream length");}
                    if(p.received==sizeof(Ack)){
                        if(p.ack.magic==magic && p.ack.run==run && p.ack.sequence==seq && p.ack.checksum==checksum)p.durable=true;
                        else {require(udp && p.ack.magic==magic && p.ack.run==run && p.ack.sequence<seq,"valid current or stale ACK");p.received=0;}
                    }
                    if(!p.durable)receive(i);
                } else {
                    require(tag>=0x10000,"known completion");size_t i=(tag-0x10000)/256,j=(tag-0x10000)%256;
                    require(i<peers.size() && j<parts,"send identity");auto& p=peers[i];require(p.pending>0,"pending send");--p.pending;
                    if(udp)require(result==static_cast<int>(packets[j].size()),"complete UDP send");
                    else {require(result>0,"TCP send makes progress");p.sent+=result;
                        require(p.sent<=packets[j].size(),"TCP send length");if(p.sent<packets[j].size())send(i);}
                }
            }
            auto now=Clock::now();
            if(written && synced && !local)local=ns(start,now);
            if(local && !committed && std::any_of(peers.begin(),peers.end(),[](const Peer& p){return p.durable;}))committed=ns(start,now);
            if(local && std::all_of(peers.begin(),peers.end(),[](const Peer& p){return p.durable && !p.pending;}))break;
            require(ns(start,now)<10000000000ULL,"commit/drain retry deadline");
            if(udp)for(size_t i=0;i<peers.size();++i){auto& p=peers[i];
                if(!p.durable && !p.pending && ns(p.last_send,now)>=static_cast<uint64_t>(retry_us)*1000){send(i);retries+=parts;}}
            ring.submit();
        }
        if(seq>warmup)timings.push_back({committed,ns(start,Clock::now()),local,peers[0].ack.durable_ns,
                                       peers.size()>1 ? peers[1].ack.durable_ns : 0,retries});
    }
    for(auto& p:peers)close(p.fd);
    std::ofstream out(output);require(bool(out),"open commit samples");
    out<<"commit_ns,all_followers_ns,leader_durable_ns,follower1_write_ns,follower2_write_ns,retried_packets\n";
    for(const auto& t:timings)out<<t.commit<<','<<t.all<<','<<t.local<<','<<t.remote1<<','<<t.remote2<<','<<t.retries<<'\n';
    out.close();require(bool(out),"write commit samples");
    log.verify();std::cout<<"{\"samples\":"<<timings.size()<<",\"warmup\":"<<warmup<<",\"verified_records\":"<<count
                        <<",\"remote_voters\":"<<peers.size()<<",\"fragments_per_record\":"<<parts<<"}\n";
}

int main(int argc,char** argv)try {
    if(argc==7 && std::string(argv[1])=="recover") {
        recover(argv[2],argv[3],std::stoul(argv[4]),std::stoul(argv[5]),std::stoull(argv[6]));
        std::cout<<"{\"verified_records\":"<<argv[5]<<"}\n";return 0;
    }
    if(argc!=15)throw std::runtime_error("bench ROLE TRANSPORT PEER1 PEER2 PORT BYTES SAMPLES PATH METHOD LAYOUT OUTPUT RUN PACKET_BYTES RETRY_US");
    std::string role=argv[1],transport=argv[2];
    require(role=="leader" || role=="follower","benchmark role");
    require(transport=="tcp" || transport=="udp","transport");
    int port=std::stoi(argv[5]),retry=std::stoi(argv[14]);size_t bytes=std::stoul(argv[6]),samples=std::stoul(argv[7]),packet=std::stoul(argv[13]);
    uint64_t run=std::stoull(argv[12]);require(packet>=1024 && packet<=8973 && retry>=100 && retry<=100000,"bounded UDP tuning");
    Log log(argv[8],argv[9],argv[10],bytes,samples+warmup,run);
    if(role=="leader")leader(transport=="udp",{argv[3],argv[4]},port,bytes,samples+warmup,log,run,packet,retry,argv[11]);
    else follower(transport=="udp",argv[3],port,bytes,samples+warmup,log,run,packet,argv[11]);
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
