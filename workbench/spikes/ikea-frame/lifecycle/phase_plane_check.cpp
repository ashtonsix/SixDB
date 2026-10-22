#include "phase_plane.h"
#include "phase_owner.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <random>
#include <vector>
#include <cstdio>

namespace p=frame_lifecycle::phase;
int main() {
    std::mt19937_64 random(1729); unsigned checks=0;
    for (unsigned width=1;width<=64;++width) for (unsigned rows:{3u,15u,255u,256u}) {
        const auto maximum=width==64?UINT64_MAX:(p::u64{1}<<width)-1;
        std::vector<p::u64> expected(rows);
        for (auto& x:expected) x=random()&maximum;
        const auto size=p::fe::plane_bytes(width,rows);
        std::vector<std::uint8_t> owned(13+size+17,0xcd);auto* body=owned.data()+13;
        p::fe::pack(width,expected,body);
        const std::array edits{p::PlaneEdit{0,maximum},p::PlaneEdit{rows-1,0},p::PlaneEdit{rows/2,maximum/2},p::PlaneEdit{0,maximum-1}};
        for (const auto& e:edits) expected[e.row]=e.value;
        p::replace_private_plane(width,rows,body,edits);
        for (unsigned i=0;i<rows;++i) {p::require(p::fe::plane_kernels(width).point(body,i)==expected[i],"private plane point differs");++checks;}
        p::require(std::all_of(owned.begin(),owned.begin()+13,[](auto x){return x==0xcd;}) &&
                   std::all_of(owned.end()-17,owned.end(),[](auto x){return x==0xcd;}),"private plane wrote beyond its extent");
    }
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* inaccessible=static_cast<std::uint8_t*>(mmap(nullptr,page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    p::require(inaccessible!=MAP_FAILED,"mmap failed");
    p::replace_private_plane(0,0,inaccessible,{});
    const auto child=fork();p::require(child>=0,"fork failed");
    if (!child) {const std::array edit{p::PlaneEdit{0,1}};p::replace_private_plane(13,256,inaccessible,edit);_exit(0);}
    int status;p::require(waitpid(child,&status,0)==child && WIFSIGNALED(status) &&
        (WTERMSIG(status)==SIGSEGV || WTERMSIG(status)==SIGBUS),"active raw store did not fault");
    munmap(inaccessible,page);
    std::printf("%u private plane points pass: all 64 widths, partial/full tiles, unaligned guards, repeated edits, empty/active protection.\n",checks);
}
