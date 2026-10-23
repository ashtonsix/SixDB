# Pinned upstream primitives only. No workload, Frame representation or timing policy.
include_guard(GLOBAL)
include(FetchContent)

function(workbench_codec_source name repository revision archive_sha out)
  string(TOUPPER "${name}" upper)
  if(FETCHCONTENT_SOURCE_DIR_${upper})
    message(FATAL_ERROR "${name} requires the hash-verified upstream source")
  endif()
  FetchContent_Declare(${name}
    URL "https://codeload.github.com/${repository}/tar.gz/${revision}"
    URL_HASH "SHA256=${archive_sha}"
    SOURCE_SUBDIR include
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # None of these include/ directories has a CMake project. Populate the source
  # without upstream whole-project flag auto-detection or benchmark downloads.
  FetchContent_MakeAvailable(${name})
  set(${out} "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(workbench_codec_settings target)
  target_compile_features(${target} PUBLIC cxx_std_23)
  target_compile_options(${target} PRIVATE -O3 -fno-fast-math -ffp-contract=off)
  if(SIXDB_MARCH)
    target_compile_options(${target} PRIVATE "-march=${SIXDB_MARCH}")
  endif()
  if(sixdb_clang_tune)
    target_compile_options(${target} PRIVATE "-mtune=${sixdb_clang_tune}")
  endif()
endfunction()

set(WORKBENCH_TURBOPFOR_REVISION "1f646d98d65ee91befd3066f351f2cf386593442")
set(WORKBENCH_FASTLANES_REVISION "f0edc1020a538f1f8098640fce8347c9ac247a0d")
set(WORKBENCH_ALP_REVISION "31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce")

workbench_codec_source(frame_turbopfor powturbo/TurboPFor-Integer-Compression
  ${WORKBENCH_TURBOPFOR_REVISION}
  2acebd3031b20f67d8b59e2350607d7505d9b474f025e0983c5186e2550ba3c0 turbo)
find_program(workbench_codec_clang NAMES clang-21 REQUIRED)
find_program(workbench_codec_make NAMES make REQUIRED)
set(codec_jobs "$ENV{SIXDB_BUILD_JOBS}")
if(NOT codec_jobs MATCHES "^[1-9][0-9]*$")
  set(codec_jobs 1)
endif()
set(codec_tune "${sixdb_clang_tune}")
if(NOT codec_tune)
  set(codec_tune generic)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  # Upstream intentionally builds non-AVX2 and AVX2 TUs separately; compiling
  # every TU with AVX2 omits scalar/API definitions and duplicates other symbols.
  set(turbo_flags "_SSE=-march=corei7-avx -mtune=${codec_tune}"
                  "_AVX2=-march=x86-64-v3 -mtune=${codec_tune}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(turbo_flags "_SSE=-march=armv8-a+simd -mtune=${codec_tune}")
else()
  message(FATAL_ERROR "TurboPFor provider needs a qualified native build for this ISA")
endif()
string(SHA256 turbo_build_id "${WORKBENCH_TURBOPFOR_REVISION};${workbench_codec_clang};${turbo_flags}")
set(turbo_build "${CMAKE_CURRENT_BINARY_DIR}/turbopfor-${turbo_build_id}")
file(MAKE_DIRECTORY "${turbo_build}")
add_custom_command(OUTPUT "${turbo_build}/libic.a"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${turbo}" "${turbo_build}"
  COMMAND ${workbench_codec_make} "-j${codec_jobs}" libic.a
    "CC=${workbench_codec_clang}" "CXX=${CMAKE_CXX_COMPILER}" ${turbo_flags}
  WORKING_DIRECTORY "${turbo_build}"
  VERBATIM)
add_custom_target(workbench_turbopfor_build DEPENDS "${turbo_build}/libic.a")
add_library(workbench_turbopfor STATIC IMPORTED GLOBAL)
set_target_properties(workbench_turbopfor PROPERTIES
  IMPORTED_LOCATION "${turbo_build}/libic.a"
  INTERFACE_INCLUDE_DIRECTORIES "${turbo}/include;${turbo}/lib/include_")
add_dependencies(workbench_turbopfor workbench_turbopfor_build)
add_library(workbench::turbopfor ALIAS workbench_turbopfor)

workbench_codec_source(frame_fastlanes cwida/FastLanes
  ${WORKBENCH_FASTLANES_REVISION}
  91f6807c6e1f42af54e9e397892e20d6996d3917279cadf9aac9cb39de4fd7f5 fls)
add_library(workbench_fastlanes_ffor STATIC EXCLUDE_FROM_ALL
  "${fls}/src/alp/src/fastlanes_ffor.cpp"
  "${fls}/src/alp/src/fastlanes_gen_ffor.cpp"
  "${fls}/src/alp/src/fastlanes_unffor.cpp"
  "${fls}/src/alp/src/fastlanes_gen_unffor.cpp")
target_include_directories(workbench_fastlanes_ffor SYSTEM PUBLIC "${fls}/src/include")
workbench_codec_settings(workbench_fastlanes_ffor)
add_library(workbench::fastlanes_ffor ALIAS workbench_fastlanes_ffor)

workbench_codec_source(frame_alp cwida/ALP ${WORKBENCH_ALP_REVISION}
  080807a58508ad4041f00251c14feae3a984d7630046f55aa42b375fb491b6f3 alp)
add_library(workbench_alp STATIC EXCLUDE_FROM_ALL
  "${alp}/src/falp.cpp"
  "${alp}/src/fastlanes_generated_unffor.cpp"
  "${alp}/src/fastlanes_generated_ffor.cpp"
  "${alp}/src/fastlanes_ffor.cpp"
  "${alp}/src/fastlanes_unffor.cpp")
target_include_directories(workbench_alp SYSTEM PUBLIC "${alp}/include")
workbench_codec_settings(workbench_alp)
add_library(workbench::alp ALIAS workbench_alp)
