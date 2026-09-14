#include <liburing.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using timer = std::chrono::steady_clock;
void check(bool value, const std::string& operation) {
    if (!value) throw std::runtime_error(operation + ": " + std::strerror(errno));
}
uint64_t nanos(timer::time_point a, timer::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();
}
void write_at(int fd, const void* data, size_t size, uint64_t offset) {
    auto n = pwrite(fd, data, size, offset);
    check(n == static_cast<ssize_t>(size), "complete pwrite");
}
void record(void* buffer, size_t bytes, uint64_t sequence) {
    auto* data = static_cast<uint64_t*>(buffer);
    for (size_t i=0; i<bytes/8; ++i) data[i] = sequence ^ (0x9e3779b97f4a7c15ULL * (i+1));
}
int main(int argc, char** argv) try {
    if (argc != 8) throw std::runtime_error("bench PATH METHOD LAYOUT SAMPLES BYTES OUTPUT BASE_OFFSET");
    std::string path=argv[1], method=argv[2], layout=argv[3];
    size_t samples=std::stoul(argv[4]), bytes=std::stoul(argv[5]), warmup=32;
    uint64_t base=std::stoull(argv[7]), total=(samples+warmup)*bytes;
    bool raw=layout=="raw", direct=method.find("direct")!=std::string::npos;
    bool extra_flush=method=="direct-dsync-fdatasync";
    bool dsync=method.ends_with("dsync") || extra_flush, ring_mode=method.starts_with("uring");
    check(method=="buffered-fdatasync" || method=="direct-fdatasync" || method=="direct-dsync" ||
          method=="uring-direct-fdatasync" || method=="uring-direct-dsync" || extra_flush, "known method");
    check(layout=="extend" || layout=="fallocate" || layout=="initialized" || raw, "known layout");
    check(bytes>=512 && bytes%512==0 && samples>0 && total<=1024ULL*1024*1024, "bounded aligned experiment");
    struct stat st{};
    if (raw) { check(stat(path.c_str(), &st)==0 && S_ISBLK(st.st_mode), "raw block device"); }
    else { check(base==0, "file offset"); }
    int init_fd=open(path.c_str(), O_RDWR | (raw ? 0 : O_CREAT|O_TRUNC), 0600); check(init_fd>=0,"open setup");
    if(raw) {
        uint64_t capacity=0;
        int sector=0;
        check(ioctl(init_fd,BLKGETSIZE64,&capacity)==0 && ioctl(init_fd,BLKSSZGET,&sector)==0,"block geometry");
        check(base%sector==0 && bytes%sector==0 && base<=capacity && total<=capacity-base,"raw range within device");
    }
    void* buf=nullptr; check(posix_memalign(&buf,4096,std::max<size_t>(bytes,1024*1024))==0,"aligned buffer");
    record(buf, std::max<size_t>(bytes,1024*1024), 0);
    auto preparation=timer::now();
    if (layout=="fallocate" || layout=="initialized") {
        int error=posix_fallocate(init_fd,0,total); if(error) { errno=error; check(false,"posix_fallocate"); }
    }
    if (layout=="initialized" || raw) {
        for(uint64_t off=0; off<total;) {
            size_t n=std::min<uint64_t>(1024*1024,total-off);
            write_at(init_fd,buf,n,base+off); off+=n;
        }
    }
    check(fdatasync(init_fd)==0,"setup fdatasync"); check(close(init_fd)==0,"close setup");
    if (!raw) {
        int parent=open(std::filesystem::path(path).parent_path().c_str(), O_RDONLY|O_DIRECTORY);
        check(parent>=0 && fsync(parent)==0,"persist parent directory"); close(parent);
    }
    uint64_t setup_ns=nanos(preparation,timer::now());
    int fd=open(path.c_str(),O_RDWR | (direct ? O_DIRECT : 0) | (dsync ? O_DSYNC : 0)); check(fd>=0,"open measured file");
    io_uring ring{};
    if(ring_mode) { int result=io_uring_queue_init(16,&ring,0); if(result<0){errno=-result;check(false,"io_uring_queue_init");} }
    std::vector<uint64_t> times; times.reserve(samples);
    std::vector<uint64_t> write_times, sync_times; write_times.reserve(samples); sync_times.reserve(samples);
    for(size_t i=0; i<samples+warmup; ++i) {
        record(buf,bytes,i+1);
        auto start=timer::now(), written=start;
        if(ring_mode) {
            auto* sqe=io_uring_get_sqe(&ring); check(sqe,"write SQE");
            io_uring_prep_write(sqe,fd,buf,bytes,base+i*bytes); sqe->user_data=1;
            if(!dsync) {
                sqe->flags|=IOSQE_IO_LINK;
                sqe=io_uring_get_sqe(&ring); check(sqe,"fdatasync SQE");
                io_uring_prep_fsync(sqe,fd,IORING_FSYNC_DATASYNC); sqe->user_data=2;
            }
            check(io_uring_submit(&ring)==(dsync ? 1 : 2),"submit full chain");
            bool saw_write=false,saw_sync=dsync;
            for(int j=0;j<(dsync ? 1 : 2);++j) {
                io_uring_cqe* cqe=nullptr; int result=io_uring_wait_cqe(&ring,&cqe);
                if(result<0) {errno=-result;check(false,"wait CQE");}
                int completion=cqe->res; uint64_t tag=cqe->user_data; io_uring_cqe_seen(&ring,cqe);
                if(completion<0) {errno=-completion;check(false,"I/O completion");}
                if(tag==1) {check(completion==static_cast<int>(bytes),"complete async write");written=timer::now();saw_write=true;}
                else {check(tag==2 && completion==0,"complete async fdatasync");saw_sync=true;}
            }
            check(saw_write && saw_sync,"all expected completions");
        } else {
            write_at(fd,buf,bytes,base+i*bytes); written=timer::now();
            if(!dsync || extra_flush) check(fdatasync(fd)==0,"measured fdatasync");
        }
        auto end=timer::now();
        if(i>=warmup) {times.push_back(nanos(start,end));write_times.push_back(nanos(start,written));sync_times.push_back(nanos(written,end));}
    }
    if(ring_mode) io_uring_queue_exit(&ring);
    check(close(fd)==0,"close measured file");
    // Reopen with direct reads so successful validation cannot come from page cache.
    fd=open(path.c_str(),O_RDONLY|O_DIRECT); check(fd>=0,"open verification");
    void* expected=nullptr; check(posix_memalign(&expected,4096,bytes)==0,"verification allocation");
    for(size_t i=0; i<samples+warmup; ++i) {
        check(pread(fd,buf,bytes,base+i*bytes)==static_cast<ssize_t>(bytes),"verification direct read");
        record(expected,bytes,i+1); check(memcmp(buf,expected,bytes)==0,"record identity");
    }
    close(fd);free(buf);free(expected);
    std::ofstream out(argv[6]);check(bool(out),"open output");out<<"latency_ns,write_completion_ns,sync_remainder_ns\n";
    for(size_t i=0;i<samples;++i)out<<times[i]<<','<<write_times[i]<<','<<sync_times[i]<<'\n';
    std::cout<<"{\"samples\":"<<samples<<",\"bytes\":"<<bytes<<",\"warmup\":"<<warmup<<",\"setup_ns\":"<<setup_ns<<",\"verified_records\":"<<samples+warmup<<",\"base_offset\":"<<base<<"}\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
