// Descriptive construction coverage; no timings or policy selection.
#include "../encoding/material.h"
#include <iomanip>
#include <iostream>
#include <set>

namespace fe=frame_encoding;
int main(int argc,char** argv) {
    if (argc<2) return 2;
    for (int arg=1;arg<argc;++arg) {
        std::ifstream input(argv[arg],std::ios::binary|std::ios::ate);
        const auto bytes=input.tellg();
        if (!input || bytes<8192*8 || bytes%8) return 2;
        std::vector<fe::u64> values(std::size_t(bytes)/8);input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(values.data()),bytes)) return 2;
        // Predeclared systematic positions; align to actual4096-row groups.
        std::set<std::size_t> offsets;
        for (unsigned sample=0;sample<20;++sample)
            offsets.insert((values.size()-8192)*sample/19/4096*4096);
        for (const auto first:offsets) {
            const auto column=fe::encode(std::span(values).subspan(first,8192),{.bind_decoders=false});
            std::array<unsigned,7> kinds{};std::array<unsigned,65> widths{};
            for (const auto& d:column.blocks) {++kinds[unsigned(d.type)];++widths[d.width];}
            std::cout<<"{\"input\":"<<std::quoted(argv[arg])<<",\"input_rows\":"<<values.size()
                <<",\"first_row\":"<<first<<",\"rows\":8192,\"payload_bytes\":"<<column.payload.size()
                <<",\"eligible_kind_blocks\":"<<kinds[2]+kinds[3]<<",\"kind_blocks\":[";
            for (unsigned i=0;i<kinds.size();++i) std::cout<<(i?",":"")<<kinds[i];
            std::cout<<"],\"width_blocks\":[";
            for (unsigned i=0;i<widths.size();++i) std::cout<<(i?",":"")<<widths[i];
            std::cout<<"]}\n";
        }
    }
}
