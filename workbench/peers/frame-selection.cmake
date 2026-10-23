# Optional pinned upstream primitives. No Frame representation or timing policy.
include_guard(GLOBAL)
include(FetchContent)
if(NOT CMAKE_C_COMPILER_LOADED)
  find_program(workbench_selection_clang NAMES clang-21 REQUIRED)
  set(CMAKE_C_COMPILER "${workbench_selection_clang}" CACHE FILEPATH "Pinned C compiler")
  enable_language(C)
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL "21.1.8")
  message(FATAL_ERROR "Selection controls require pinned Clang 21.1.8 for C")
endif()

function(workbench_selection_file filename url sha out)
  set(folder "${CMAKE_BINARY_DIR}/_deps/frame-selection/croaring-v5.3.2")
  file(MAKE_DIRECTORY "${folder}")
  set(path "${folder}/${filename}")
  if(EXISTS "${path}")
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL sha)
      message(FATAL_ERROR "Cached CRoaring file hash mismatch: ${path}")
    endif()
  else()
    file(DOWNLOAD "${url}" "${path}.download" EXPECTED_HASH "SHA256=${sha}" TLS_VERIFY ON)
    file(RENAME "${path}.download" "${path}")
  endif()
  set(${out} "${path}" PARENT_SCOPE)
endfunction()

function(workbench_selection_settings target)
  target_compile_options(${target} PRIVATE -O3 -fno-fast-math -ffp-contract=off)
  if(SIXDB_MARCH)
    target_compile_options(${target} PRIVATE "-march=${SIXDB_MARCH}")
  endif()
  if(sixdb_clang_tune)
    target_compile_options(${target} PRIVATE "-mtune=${sixdb_clang_tune}")
  endif()
  if(SIXDB_FRAME_SELECTION_SANITIZE)
    if(NOT SIXDB_FRAME_SELECTION_SANITIZERS)
      set(SIXDB_FRAME_SELECTION_SANITIZERS "address,undefined")
    endif()
    target_compile_options(${target} PRIVATE -O1 "-fsanitize=${SIXDB_FRAME_SELECTION_SANITIZERS}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${target} INTERFACE "-fsanitize=${SIXDB_FRAME_SELECTION_SANITIZERS}")
  endif()
endfunction()

set(WORKBENCH_CROARING_REVISION "5fce11dbf8f8227125a466ce895266967e708816")
set(WORKBENCH_HIGHWAY_REVISION "2607d3b5b0113992fe84d3848859eae13b3b52c1")
# Official release amalgamation avoids downloading ~100 MB of benchmark data.
workbench_selection_file(roaring.c
  "https://github.com/RoaringBitmap/CRoaring/releases/download/v5.3.2/roaring.c"
  81ae778a927108500a7d577ae3ff44fa1dfa5c46dfe0f0d0bedd8e4ae624efe2 roaring_c)
workbench_selection_file(roaring.h
  "https://github.com/RoaringBitmap/CRoaring/releases/download/v5.3.2/roaring.h"
  5a09d9e2996b8c0c545b8147996a3b008bcad1a991c559c9b4ecac3120a2b468 roaring_h)
get_filename_component(roaring_include "${roaring_h}" DIRECTORY)
add_library(workbench_croaring STATIC EXCLUDE_FROM_ALL "${roaring_c}")
target_compile_features(workbench_croaring PRIVATE c_std_11)
target_include_directories(workbench_croaring SYSTEM PUBLIC "${roaring_include}")
workbench_selection_settings(workbench_croaring)
add_library(workbench::croaring ALIAS workbench_croaring)

if(FETCHCONTENT_SOURCE_DIR_FRAME_SELECTION_HIGHWAY)
  message(FATAL_ERROR "Highway provider requires the hash-verified source archive")
endif()
FetchContent_Declare(frame_selection_highway
  URL "https://codeload.github.com/google/highway/tar.gz/${WORKBENCH_HIGHWAY_REVISION}"
  URL_HASH SHA256=741d705781e0b3e406beda8f1f994fbae01321237ce8023a1ad90fbaf7940c25
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR __sixdb_source_only__)
FetchContent_MakeAvailable(frame_selection_highway)
set(highway "${frame_selection_highway_SOURCE_DIR}")
# Same core source list as upstream, without its tests, examples or other contrib.
add_library(workbench_highway_core STATIC EXCLUDE_FROM_ALL
  "${highway}/hwy/abort.cc" "${highway}/hwy/aligned_allocator.cc"
  "${highway}/hwy/nanobenchmark.cc" "${highway}/hwy/per_target.cc"
  "${highway}/hwy/perf_counters.cc" "${highway}/hwy/print.cc"
  "${highway}/hwy/profiler.cc" "${highway}/hwy/targets.cc" "${highway}/hwy/timer.cc")
target_include_directories(workbench_highway_core SYSTEM PUBLIC "${highway}")
target_compile_definitions(workbench_highway_core PUBLIC HWY_STATIC_DEFINE)
target_compile_features(workbench_highway_core PUBLIC cxx_std_23)
workbench_selection_settings(workbench_highway_core)
# Real unmodified dynamic-dispatch TUs: all integer lane sizes and both payload
# forms. Floats, uint128 keys, selection and partial-sort APIs are not qualified.
set(vqsort_sources "${highway}/hwy/contrib/sort/vqsort.cc")
foreach(type IN ITEMS u16 u32 u64 i16 i32 i64 kv64 kv128)
  foreach(direction IN ITEMS a d)
    list(APPEND vqsort_sources "${highway}/hwy/contrib/sort/vqsort_${type}${direction}.cc")
  endforeach()
endforeach()
add_library(workbench_vqsort STATIC EXCLUDE_FROM_ALL ${vqsort_sources})
target_link_libraries(workbench_vqsort PUBLIC workbench_highway_core)
workbench_selection_settings(workbench_vqsort)
add_library(workbench::vqsort ALIAS workbench_vqsort)
