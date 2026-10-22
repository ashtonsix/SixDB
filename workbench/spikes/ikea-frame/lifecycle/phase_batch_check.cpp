#include "phase_batch.h"
#include "phase_owner.h"
#include <map>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

namespace p=frame_lifecycle::phase;
int main() {
    std::mt19937_64 random(713);unsigned long long checks=0;
    for (unsigned trial=0;trial<96;++trial) {
        std::array<p::LogicalRow,p::rows_per_group> base;
        for (unsigned i=0;i<base.size();++i) base[i]={random(),i%13!=0,i%17!=0};
        for (auto& row:base) row.valid&=row.present;
        std::map<unsigned,p::LogicalRow> expected;
        std::vector<p::OverrideRow> old;
        for (unsigned batch=0;batch<12;++batch) {
            std::vector<p::Edit> commands;
            const p::BlockSelection mask{batch%4==0?0u:batch%4==1?UINT32_MAX:batch%4==2?0xaaaaaaaau:0x0000ffffu};
            for (unsigned i=0;i<193;++i) {
                const unsigned row=batch%3?random()%p::rows:i%8;
                p::LogicalRow next{random(),bool(random()%5),bool(random()%7)};next.valid&=next.present;
                // Returning to the immutable base removes an existing override.
                if (row<p::rows_per_group && i%7==0) next=base[row];
                commands.push_back({row,next});
                if (row<p::rows_per_group && mask.row(row)) expected[row]=next;
            }
            p::IndexedBatch index(commands,mask);
            unsigned selected=0;
            for (unsigned b=0;b<p::groups*p::blocks_per_group;++b) {
                auto block=index.block(b);selected+=block.size();
                for (const auto& e:block) {p::require(e.row/256==b && mask.row(e.row),"batch routed an excluded row");++checks;}
            }
            p::require(selected==index.ordered.size(),"batch offsets lost commands");
            const p::Overlay h{std::uint16_t(old.size()),1729};std::vector<std::byte> body(p::Overlay::bytes(old.size()));
            p::copy_overlay(old,body,h.basis);
            unsigned base_reads=0;
            old=p::merge_overrides(h,{body.data(),body.size()},index.group(0),0,index.distinct[0],
                [&](unsigned row){++base_reads;return base[row];});
            unsigned previous=p::rows_per_group;
            std::map<unsigned,p::LogicalRow> actual;
            for (const auto& e:old) {
                p::require(previous==p::rows_per_group || previous<e.position,"merge lost strictly ordered unique rows");previous=e.position;
                p::require(p::same_logical(e.original,base[e.position]),"merge changed its immutable original contribution");
                actual[e.position]=e.current;
            }
            for (unsigned row=0;row<p::rows_per_group;++row) {
                const auto wanted=expected.contains(row)?expected[row]:base[row];
                const auto found=actual.contains(row)?actual[row]:base[row];
                p::require(p::same_logical(wanted,found),"merged assignment differs from sequential oracle");++checks;
                p::require(actual.contains(row)==!p::same_logical(base[row],wanted),"merge retained an unnecessary correction");
            }
            p::require(base_reads<=index.distinct[0],"merge repeated a base lookup for one row");
            // Row sorting must preserve every original assignment to that row.
            for (unsigned row=0;row<8;++row) {
                std::vector<p::u64> wanted,got;
                for (const auto& e:commands) if (e.row==row && mask.row(row)) wanted.push_back(e.next.value);
                for (const auto& e:index.block(0)) if (e.row==row) got.push_back(e.next.value);
                p::require(wanted==got,"index changed same-row assignment order");
            }
        }
    }
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* unreadable=mmap(nullptr,page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    p::require(unreadable!=MAP_FAILED,"mmap failed");
    p::IndexedBatch none({static_cast<const p::Edit*>(unreadable),7},{0});
    p::require(none.ordered.empty(),"empty coarse selection touched commands");munmap(unreadable,page);
    std::printf("%llu batch checks pass: ordered duplicates, masks/groups/blocks, immutable originals, exact removal, bounded base reads and protected none.\n",checks);
}
