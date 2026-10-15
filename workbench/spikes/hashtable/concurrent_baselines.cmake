include_guard(GLOBAL)
include(FetchContent)
find_package(Threads REQUIRED)

# Official Boost 1.90.0 archive fixes Unordered at commit 11e05a3c4c449a46819f0030e7d105fbc154d476
# and all support headers at the same release. Release manifest:
# https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2.json
# No Boost compiled libraries, tests, or install targets are needed here.
set(SIXDB_HASHTABLE_BOOST_UNORDERED_REVISION "11e05a3c4c449a46819f0030e7d105fbc154d476")
set(SIXDB_HASHTABLE_GTL_REVISION "4c0a7a1d4a86143247b6ea7a5e4c7d6104d78590") # v1.2.0
if(FETCHCONTENT_SOURCE_DIR_HASHTABLE_BOOST OR FETCHCONTENT_SOURCE_DIR_HASHTABLE_GTL)
  message(FATAL_ERROR "concurrent baselines do not accept unverified dependency source overrides")
endif()
FetchContent_Declare(hashtable_boost
  URL "https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2"
  URL_HASH SHA256=49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR sixdb_headers_only)
FetchContent_Declare(hashtable_gtl
  URL "https://codeload.github.com/greg7mdp/gtl/tar.gz/${SIXDB_HASHTABLE_GTL_REVISION}"
  URL_HASH SHA256=42bc863a546e31af4e017fb60c4b225a13de80ce167b2e442fdcb86ec2e4da51
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR sixdb_headers_only)
FetchContent_MakeAvailable(hashtable_boost hashtable_gtl)
if(NOT TARGET hashtable_abseil_baseline)
  include("${CMAKE_CURRENT_LIST_DIR}/reference/baseline.cmake")
endif()
add_library(hashtable_concurrent_baselines INTERFACE)
target_compile_features(hashtable_concurrent_baselines INTERFACE cxx_std_23)
target_include_directories(hashtable_concurrent_baselines SYSTEM INTERFACE
  "${hashtable_boost_SOURCE_DIR}" "${hashtable_gtl_SOURCE_DIR}/include")
target_include_directories(hashtable_concurrent_baselines INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(hashtable_concurrent_baselines INTERFACE absl::inlined_vector Threads::Threads)

add_executable(hashtable_design_concurrent_baselines_check
  "${CMAKE_CURRENT_LIST_DIR}/concurrent_baselines_check.cpp")
function(hashtable_concurrent_check_settings target)
  sixdb_target(${target})
  target_link_libraries(${target} PRIVATE hashtable_concurrent_baselines)
  target_compile_definitions(${target} PRIVATE NDEBUG)
  target_compile_options(${target} PRIVATE
    -fno-vectorize -fno-slp-vectorize -mllvm -inline-threshold=1000)
endfunction()
hashtable_concurrent_check_settings(hashtable_design_concurrent_baselines_check)
# Keep all-width validation from becoming a single expensive template TU.
# Use -j1 on the local VM; each width remains independently incremental.
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  set(part hashtable_design_concurrent_width_${width})
  add_library(${part} OBJECT "${CMAKE_CURRENT_LIST_DIR}/concurrent_baselines_check.cpp")
  hashtable_concurrent_check_settings(${part})
  target_compile_definitions(${part} PRIVATE HT_CONCURRENT_K=${width})
  target_sources(hashtable_design_concurrent_baselines_check PRIVATE $<TARGET_OBJECTS:${part}>)
endforeach()
add_test(NAME hashtable_design_concurrent_baselines_check
  COMMAND hashtable_design_concurrent_baselines_check check)
