#include "material.h"
#include "journal.h"
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tp=ikea::tuplepack;
using namespace publication;
namespace {
constexpr std::size_t size=8192, base_a=128, base_b=4096+256, summary_at=64;
void check(bool ok) { if(!ok) std::abort(); }
template<class T,class E> T get(std::expected<T,E> result) { check(bool(result));return std::move(*result); }
void summary(Bytes& b,std::uint64_t sum) {for(unsigned i=0;i<8;++i)b[summary_at+i]=std::uint8_t(sum>>(8*i));}
Bytes expected(unsigned epoch) {
  Bytes b(size);for(std::size_t i=0;i<size;++i)b[i]=std::uint8_t((i*29+47)%251);
  if(epoch) {
    const auto a=(epoch+2)%8,bv=(2*epoch+1)%8;
    b[base_a+17]=std::uint8_t((b[base_a+17]&~14U)|(a<<1));
    b[base_b+11]=std::uint8_t((b[base_b+11]&~14U)|(bv<<1));
    summary(b,a+bv);
  } else {
    summary(b,((b[base_a+17]>>1)&7)+((b[base_b+11]>>1)&7));
  }
  return b;
}
struct Head { unsigned epoch;Cut cut;std::uint64_t sum; };
bool deliver(const std::filesystem::path& path,const Bytes& bytes) {
  const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);check(fd>=0);
  std::size_t at{};
  while(at<bytes.size()) {
    const auto n=::write(fd,bytes.data()+at,bytes.size()-at);
    if(n<=0) {::close(fd);return false;} at+=static_cast<std::size_t>(n);
  }
  const bool ok=::fdatasync(fd)==0;::close(fd);return ok;
}
void verify(Material& material,const Head& head) {
  auto bytes=material.read(head.cut,0,size);check(bytes==expected(head.epoch));
  std::uint64_t sum{};for(unsigned i=0;i<8;++i)sum|=std::uint64_t(bytes[summary_at+i])<<(8*i);
  check(sum==head.sum);
}
}
int main(int argc,char** argv) {
  if(argc==3) {
    Recovery reopened(argv[1]);const auto epoch=static_cast<unsigned>(std::stoul(argv[2]));
    check(reopened.latest()==epoch && reopened.read_latest(epoch,0,size)==expected(epoch));
    return 0;
  }
  const auto path=std::filesystem::temp_directory_path()/std::filesystem::path("sixdb-native-composition-"+std::to_string(::getpid()));
  auto* home=static_cast<std::uint8_t*>(::mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  check(home!=MAP_FAILED);
  auto initial=expected(0);std::copy(initial.begin(),initial.end(),home);
  const std::array<tp::code,1> codes{{{0,1,3}}};const std::array<std::uint8_t,1> ranks{0};
  const auto format=get(tp::layout::make(1,codes));const auto writer=get(tp::write_plan<8>::make(format,ranks));
  const auto reader=get(tp::read_plan<8>::make(format,ranks));
  Counts counts;Material material(Kind::radix_pages,size,true,0,counts);
  std::vector<Head> retained;retained.reserve(6);
  retained.push_back({0,material.seed(initial),std::uint64_t(((initial[base_a+17]>>1)&7)+((initial[base_b+11]>>1)&7))});
  std::atomic<const Head*> visible{&retained.back()};Sink sink(size*2);
  std::array<std::promise<void>,2> gates;
  std::array<std::future<bool>,2> transfers;
  const auto derived=path.string()+"-derived-2";
  {
    Journal journal(path,initial);
    for(unsigned e=1;e<=5;++e) {
      auto a=get(tp::view::attach(format,std::as_writable_bytes(std::span{home+base_a,std::size_t(31)}),2,3,17));
      auto b=get(tp::view::attach(format,std::as_writable_bytes(std::span{home+base_b,std::size_t(31)}),2,3,11));
      auto oa=get(tp::bind_writer(writer,a)),ob=get(tp::bind_writer(writer,b));
      auto group=get(tp::author::bind_group(oa,ob));
      const decltype(group)::input_type input{(e+2)%8,(2*e+1)%8};
      std::array<ikea::journal::entry,2> slots{};ikea::journal damage{slots};
      check(bool(group.set(0,input,damage)) && damage.used==2);
      Batch patches;
      for(auto effect:damage.entries()) {
        const auto base=effect.source==&a?base_a:base_b;
        check((effect.source==&a || effect.source==&b) && effect.plane==0);
        const auto offset=base+effect.offset;
        patches.push_back({offset,Bytes(home+offset,home+offset+effect.size)});
      }
      auto ra=get(tp::bind_reader(reader,a)),rb=get(tp::bind_reader(reader,b));
      const auto sum=get(ra.get(0))+get(rb.get(0));
      Bytes encoded(8);for(unsigned i=0;i<8;++i)encoded[i]=std::uint8_t(sum>>(8*i));
      std::copy(encoded.begin(),encoded.end(),home+summary_at);patches.push_back({summary_at,std::move(encoded)});
      // Own bytes after the native call, while this owner excludes new writes.
      auto stable=capture(patches,counts);journal.append(e,*stable);
      retained.push_back({e,material.update(retained.back().cut,std::move(stable),e),sum});
      visible.store(&retained.back(),std::memory_order_release);verify(material,*visible.load(std::memory_order_acquire));
      if(e<=2) {
        check(sink.submit(e,retained.back().cut,size));
        auto body=material.read(retained.back().cut,0,size);
        transfers[e-1]=std::async(std::launch::async,
          [go=gates[e-1].get_future(),body=std::move(body),target=e==1?std::string("/dev/full"):derived]() mutable {
            go.get();return deliver(target,body);
          });
      }
      if(e==3) {
        check(!sink.submit(e,retained.back().cut,size) && sink.installed()==0);
        const auto epoch=std::to_string(e);
        auto child=::fork();check(child>=0);
        if(!child) {::execl("/proc/self/exe","composition_probe",path.c_str(),epoch.c_str(),nullptr);::_exit(123); }
        int status{};check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
      }
      for(const auto& old:retained)verify(material,old);
    }
    gates[1].set_value();check(transfers[1].get() && sink.complete(1,true));
    gates[0].set_value();check(!transfers[0].get() && sink.complete(0,false) && sink.installed()==2);
    const int fd=::open(derived.c_str(),O_RDONLY|O_CLOEXEC);check(fd>=0);Bytes shipped(size);
    check(::read(fd,shipped.data(),size)==static_cast<ssize_t>(size));::close(fd);
    check(shipped==expected(2));std::filesystem::remove(derived);
    check(visible.load()->epoch==5 && sink.pending()==0);
  }
  check(::munmap(home,size)==0);
  {Recovery reopen(path);check(reopen.read_latest(5,0,size)==expected(5));}
  std::filesystem::remove(path);
  std::puts("real packed native writes -> stable effects -> durable replay basis -> coherent bytes/sum head; stalled and failed derived sink; retained old cuts and fresh recovery: ok");
}
