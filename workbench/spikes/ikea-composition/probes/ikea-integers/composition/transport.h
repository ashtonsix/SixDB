#pragma once
#include "native_ops.h"
#include <array>

namespace ikea::integers::composition {
struct Instruction;
#if defined(__aarch64__)
#define I12_VALUES_ARGS uint16x8_t values_lo,uint16x8_t values_hi
#define I12_VALUES_FORWARD values_lo,values_hi
#define I12_VALUES Values16{values_lo,values_hi}
#define I12_VALUES_EXPAND(v) (v).lo,(v).hi
#else
#define I12_VALUES_ARGS __m256i values
#define I12_VALUES_FORWARD values
#define I12_VALUES values
#define I12_VALUES_EXPAND(v) (v)
#endif
#define I12_STAGE_ARGS const Instruction* cursor,const std::uint8_t* tile,unsigned group,unsigned cutoff,std::uint64_t acc,unsigned mask,I12_VALUES_ARGS
#define I12_STAGE_FORWARD cursor+1,tile,group,cutoff,acc,mask,I12_VALUES_FORWARD
using Stage=std::uint64_t(*)(I12_STAGE_ARGS);
struct Instruction {Stage execute;};
struct Program {std::array<Instruction,3> stages;};
#define I12_DECLARE(name) extern "C" __attribute__((noinline)) std::uint64_t name(I12_STAGE_ARGS)
I12_DECLARE(ikea_i12_read_local);
I12_DECLARE(ikea_i12_read_scan);
I12_DECLARE(ikea_i12_read_scan_middle);
I12_DECLARE(ikea_i12_filter);
I12_DECLARE(ikea_i12_sum);
} // namespace ikea::integers::composition
