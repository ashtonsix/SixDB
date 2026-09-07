# Microarchitectural policy is independent of ISA availability. Compiler
# feature macros describe the latter; these definitions describe our choice.
set(SIXDB_TUNE "generic" CACHE STRING "Microarchitecture to tune for, independent of SIXDB_MARCH")
set_property(CACHE SIXDB_TUNE PROPERTY STRINGS generic granite-rapids zen5 neoverse-v2)

if(SIXDB_TUNE STREQUAL "generic")
  set(sixdb_clang_tune generic)
elseif(SIXDB_TUNE STREQUAL "granite-rapids")
  set(sixdb_clang_tune graniterapids)
elseif(SIXDB_TUNE STREQUAL "zen5")
  set(sixdb_clang_tune znver5)
elseif(SIXDB_TUNE STREQUAL "neoverse-v2")
  set(sixdb_clang_tune neoverse-v2)
else()
  message(FATAL_ERROR
    "Unknown SIXDB_TUNE '${SIXDB_TUNE}'; choose generic, granite-rapids, zen5, or neoverse-v2")
endif()

include(CheckCXXCompilerFlag)
string(MAKE_C_IDENTIFIER "SIXDB_SUPPORTS_MTUNE_${sixdb_clang_tune}" sixdb_tune_check)
check_cxx_compiler_flag("-mtune=${sixdb_clang_tune}" ${sixdb_tune_check})
if(NOT ${sixdb_tune_check})
  message(FATAL_ERROR
    "SIXDB_TUNE=${SIXDB_TUNE} is unsupported by the configured compiler target. "
    "Granite Rapids and Zen 5 require x86; Neoverse V2 requires AArch64.")
endif()

# Even generic is explicit, so a named -march CPU does not silently select
# processor-specific scheduling while the SixDB policy flags say generic.
target_compile_options(sixdb_build_options INTERFACE "-mtune=${sixdb_clang_tune}")
target_compile_definitions(sixdb_build_options INTERFACE
  SIXDB_TUNE_GENERIC=$<STREQUAL:${SIXDB_TUNE},generic>
  SIXDB_TUNE_GRANITE_RAPIDS=$<STREQUAL:${SIXDB_TUNE},granite-rapids>
  SIXDB_TUNE_ZEN5=$<STREQUAL:${SIXDB_TUNE},zen5>
  SIXDB_TUNE_NEOVERSE_V2=$<STREQUAL:${SIXDB_TUNE},neoverse-v2>)
