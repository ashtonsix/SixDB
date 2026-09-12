#pragma once
#include "cases.h"
#include "native.h"
#include "byte_route.h"
#include <fstream>

void describe_tuple_point(std::ostream&);

namespace tuple_runtime {
template <class Values> void json_array(std::ostream& out, const Values& values) {
    out << '[';
    bool first = true;
    for (auto value : values) { if (!first) out << ','; first = false; out << +value; }
    out << ']';
}
inline void describe_shuffle(std::ostream& out, const shuffle& s) {
    out << "{\"index\":"; json_array(out, s.index);
    out << ",\"mask\":"; json_array(out, s.mask);
    out << ",\"signed_shift\":"; json_array(out, s.shift);
    out << ",\"bit_index\":"; json_array(out, s.bit_index);
    out << ",\"even_factor\":"; json_array(out, s.even_factor);
    out << ",\"odd_factor\":"; json_array(out, s.odd_factor);
    out << ",\"avx2_index\":[";
    for (unsigned i = 0; i < 4; ++i) { if (i) out << ','; json_array(out, s.avx2_index[i]); }
    out << "],\"routes\":" << s.routes << ",\"shifting\":" << s.shifting << ",\"masking\":" << s.masking << '}';
}
inline void describe(const std::vector<case_spec>& cases, const char* path) {
    std::ofstream out(path);
    out << "{\"profile\":\"";
#if defined(__aarch64__)
    out << "neon";
#elif defined(__AVX512VBMI__)
    out << "avx512_vbmi";
#else
    out << "avx2_regcall";
#endif
    out << "\",\"semantics\":\"read holes zero; writer holes ignored; unique writer maps; preserve all unselected bits\",\"cases\":[";
    bool first = true;
    for (const auto& c : cases) {
        if (!first) out << ',';
        first = false;
        const auto r = prepare_read(c.physical(), c.map);
        const auto w = prepare_write(c.physical(), c.map);
        if (!r || !w) std::abort();
        out << "{\"name\":\"" << c.name << "\",\"tuple_bytes\":" << c.bytes << ",\"stride\":" << c.stride << ",\"codes\":[";
        for (unsigned i = 0; i < c.codes.size(); ++i) {
            if (i) out << ',';
            const auto code = c.codes[i];
            out << '[' << +code.offset << ',' << +code.shift << ',' << +code.width << ']';
        }
        out << "],\"map\":"; json_array(out, c.map);
        out << ",\"prepared_read_bytes\":" << sizeof(read_plan) << ",\"prepared_write_bytes\":" << sizeof(write_plan);
        out << ",\"read_controls\":"; describe_shuffle(out, r->operation);
        out << ",\"read_chunks\":[";
        for (unsigned i = 0; i < r->count; ++i) {
            if (i) out << ',';
            out << '[' << +r->chunks[i].offset << ',' << +r->chunks[i].bytes << ']';
        }
        out << "],\"native_read_coverage\":" << r->issued_reads << ",\"write_coverage\":" << w->issued_writes;
        std::uint64_t scalar_reads = 0, scalar8_reads = 0, scalar_old = 0;
        for (unsigned i = 0; i < 64; ++i) {
            if (r->scalar[i].width) {
                scalar_reads |= std::uint64_t(1) << r->scalar[i].offset;
                if (i < 8) scalar8_reads |= std::uint64_t(1) << r->scalar[i].offset;
            }
            if (i < w->count && w->stores[i].mask != 255)
                scalar_old |= std::uint64_t(1) << w->stores[i].offset;
        }
        out << ",\"scalar_read_coverage\":" << scalar_reads << ",\"scalar8_read_coverage\":" << scalar8_reads;
        out << ",\"scalar_old_read_coverage\":" << scalar_old;
        out << ",\"native_old_read_coverage\":" << (w->dense_native && w->needs_old ? w->issued_writes : 0);
        out << ",\"native_write_supported\":" << w->dense_native << ",\"preserve\":"; json_array(out, w->preserve);
        out << ",\"invalid_input_bits\":"; json_array(out, w->invalid_bits);
        out << ",\"write_rounds\":[";
        for (unsigned i = 0; i < w->round_count; ++i) { if (i) out << ','; describe_shuffle(out, w->rounds[i]); }
        out << "]}";
    }
    out << "],\"fusion\":{\"logical_contract\":\"32 LE uint16 values decomposed to 128 codes; two 64-byte inputs; replace selected codes and return modulo-u64 summary delta\","
           "\"placements\":\"two 32-byte views: contiguous stride64 or separate stride32 planes\","
           "\"effects_case\":\"per-invocation native value admission, two qualified before-write spans, row summary delta; leases and capacity bound outside timing; no publication or COW fault\","
           "\"bindings\":\"1 or 32 independent copies of the same schema/map; constant endpoint ignores controls\","
           "\"fixtures\":[";
    bool first_fusion = true;
    for (bool reordered : {false, true}) for (bool partial : {false, true}) {
        using namespace tuple_composition_probe;
        auto p = prepare(reordered, partial);
        if (!first_fusion) out << ','; first_fusion = false;
        out << "{\"reordered\":" << reordered << ",\"partial\":" << partial << ",\"codes\":[";
        for (unsigned i = 0; i < 128; ++i) {
            if (i) out << ',';
            auto c = p.codes[i]; out << '[' << +c.offset << ',' << +c.shift << ',' << +c.width << ']';
        }
        out << "],\"read_maps\":["; json_array(out, p.maps[0]); out << ','; json_array(out, p.maps[1]);
        out << "],\"preserve\":["; json_array(out, p.write[0].preserve); out << ','; json_array(out, p.write[1].preserve);
        out << "],\"lowered_terms\":" << p.decoded.size() << ",\"byte_identity\":" << p.byte_decoder->identity << ",\"rotating\":" << p.byte_decoder->rotating << '}';
    }
    out << "]},\"scan\":{\"contract\":\"16 code-byte sum per row; runtime map identity or (5*i+3)%16; widths 1+i%7 and shift i%(9-width); first16 bytes of stride16/64 rows\",\"shapes\":[\"16x1\",\"16x4\"],\"rows\":[1024,65536,1048576],\"pattern\":\"full sequential repeated scan\"},\"small\":";
    describe_tuple_point(out);
    out << "}\n";
    if (!out) std::abort();
}
} // namespace tuple_runtime
