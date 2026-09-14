// Experimental preparation of the actual future append range, disjoint from live writes.
#pragma once
#include <thread>
#include <atomic>
#include <sched.h>

struct Preparation {
    struct Event {uint64_t at,prepared,consumed,duration;uint32_t action;};
    std::atomic<uint64_t> prepared{0},consumed{0},pressure_until{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::thread worker;
    std::vector<Event> events;
    bool enabled=false,adaptive=false;
    uint64_t total=0,initial=0,begin=0;
    std::string path,output_path;
    std::exception_ptr failure;
    static uint64_t clock(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
    static constexpr uint64_t chunk=1024*1024,low=8*chunk,high=64*chunk;
    Preparation(const std::string& p,uint64_t bytes,bool on):enabled(on),total(bytes),path(p){
        adaptive=getenv("CFT_PREP_ADAPT") && std::string(getenv("CFT_PREP_ADAPT"))=="1";
        initial=enabled?std::min(high,total):total;prepared=initial;
    }
    void start(){
        if(!enabled || worker.joinable())return;
        begin=clock();
        worker=std::thread([this]{try{
            if(const char* cpu=getenv("CFT_PREP_CPU")){cpu_set_t set;CPU_ZERO(&set);CPU_SET(std::stoi(cpu),&set);require(!sched_setaffinity(0,sizeof(set),&set),"preparation CPU");}
            int fd=open(path.c_str(),O_RDWR);require(fd>=0,"background preparation open");
            void* buffer=nullptr;require(!posix_memalign(&buffer,4096,chunk),"preparation memory");memset(buffer,0,chunk);
            uint64_t last_event=0;
            while(!stop && prepared.load()<total){
                uint64_t at=clock(),offset=prepared.load(),used=consumed.load(),ahead=offset-used;
                bool pressure=adaptive && at<pressure_until.load() && ahead>low;
                if(ahead>=high || pressure){
                    if(at-last_event>10000000){events.push_back({at-begin,offset,used,0,pressure?2U:3U});last_event=at;}
                    std::this_thread::sleep_for(std::chrono::microseconds(100));continue;
                }
                size_t amount=std::min(chunk,total-offset);
                int e=posix_fallocate(fd,offset,amount);if(e)errno=e;require(!e,"prepare future extent");
                require(pwrite(fd,buffer,amount,offset)==static_cast<ssize_t>(amount) && !fdatasync(fd),"initialize and persist future range");
                uint64_t done=clock();prepared=offset+amount;events.push_back({done-begin,offset+amount,used,done-at,1});
            }
            free(buffer);require(!close(fd),"preparation close");
        }catch(...){failure=std::current_exception();failed.store(true,std::memory_order_release);}});
    }
    bool ready(uint64_t bytes){if(failed.load(std::memory_order_acquire))std::rethrow_exception(failure);return !enabled || bytes<=prepared.load();}
    void use(uint64_t bytes){if(enabled){uint64_t previous=consumed;while(bytes>previous && !consumed.compare_exchange_weak(previous,bytes)){}}}
    void pressure(uint64_t duration){if(enabled && adaptive && duration>200000)pressure_until=clock()+10000000;}
    void finish(const std::string& output){
        stop=true;if(worker.joinable())worker.join();
        if(!enabled)return;
        std::ofstream out(output+".preparation.csv");out<<"elapsed_ns,prepared_bytes,consumed_bytes,duration_ns,action\n";
        out<<0<<','<<initial<<",0,0,0\n";
        for(auto e:events)out<<e.at<<','<<e.prepared<<','<<e.consumed<<','<<e.duration<<','<<e.action<<'\n';
        std::ofstream meta(output+".preparation.json");meta<<"{\"steady_start_ns\":"<<begin<<",\"initial_bytes\":"<<initial<<",\"prepared_bytes\":"<<prepared.load()<<",\"consumed_bytes\":"<<consumed.load()<<",\"total_bytes\":"<<total<<",\"low_bytes\":"<<low<<",\"high_bytes\":"<<high<<",\"adaptive\":"<<(adaptive?"true":"false")<<"}\n";
        if(failure){try{std::rethrow_exception(failure);}catch(const std::exception& error){std::ofstream(output+".preparation.error.txt")<<error.what()<<'\n';}std::rethrow_exception(failure);}
    }
    ~Preparation(){try{if(!output_path.empty())finish(output_path);else{stop=true;if(worker.joinable())worker.join();}}catch(...){}}
};
