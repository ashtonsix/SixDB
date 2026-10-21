include("${PROJECT_SOURCE_DIR}/workbench/benchmarks/hashtable/abseil.cmake")
include(FetchContent)
FetchContent_Declare(wide_boost
  URL https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2
  URL_HASH SHA256=5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-cmake-needed)
FetchContent_Declare(wide_folly
  URL https://codeload.github.com/facebook/folly/tar.gz/ea41cb2f0b3c13a225a616ffef33cdfa85eb4aad
  URL_HASH SHA256=26e1d25d311a084862ed29c51a59a2b31bfdd019f5582a52c070d98b8a413bd0
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-cmake-needed)
FetchContent_MakeAvailable(wide_boost wide_folly)

# Build the actual upstream F14 support, not all of Folly. Generate its normal
# configuration header from the upstream template; detect the capabilities this
# subset uses. Optional logging, allocator and compression integrations stay off.
include(CheckCXXSourceCompiles)
include(CheckSymbolExists)
set(FOLLY_HAVE_PTHREAD 1)
set(FOLLY_HAVE_CLOCK_GETTIME 1)
set(FOLLY_HAVE_WEAK_SYMBOLS 1)
set(FOLLY_HAVE_WCHAR_SUPPORT 1)
check_cxx_source_compiles("int main() { __int128 x = 1; return int(x); }" FOLLY_HAVE_INT128_T)
check_symbol_exists(malloc_usable_size "malloc.h" FOLLY_HAVE_MALLOC_USABLE_SIZE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|aarch64")
  set(FOLLY_HAVE_UNALIGNED_ACCESS 1)
endif()
if(CMAKE_CXX_FLAGS MATCHES "sanitize=address")
  set(FOLLY_LIBRARY_SANITIZE_ADDRESS 1)
endif()
configure_file("${wide_folly_SOURCE_DIR}/CMake/folly-config.h.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/folly-config/folly/folly-config.h")
find_package(fmt REQUIRED)
add_library(wide_f14 STATIC
  "${wide_folly_SOURCE_DIR}/folly/container/detail/F14Table.cpp"
  "${wide_folly_SOURCE_DIR}/folly/lang/SafeAssert.cpp"
  "${wide_folly_SOURCE_DIR}/folly/lang/ToAscii.cpp"
  "${wide_folly_SOURCE_DIR}/folly/ScopeGuard.cpp")
sixdb_target(wide_f14)
target_compile_definitions(wide_f14 PUBLIC NDEBUG)
target_include_directories(wide_f14 SYSTEM PUBLIC "${wide_folly_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/folly-config" "${wide_boost_SOURCE_DIR}")
target_link_libraries(wide_f14 PUBLIC fmt::fmt)

set(WIDE_VALUES "8;16;32;64;128" CACHE STRING "Payload widths")
option(WIDE_LOOM "Include actual Loom Ring cold-read comparison" OFF)
set(WIDE_GEOMETRIES "32_9;32_11;32_16;32_17;32_18;40_17" CACHE STRING "W_b concrete table geometries")
set(wide_targets)
foreach(v IN LISTS WIDE_VALUES)
  foreach(geometry IN LISTS WIDE_GEOMETRIES)
    if((geometry STREQUAL "32_11" AND NOT v EQUAL 8) OR
       (geometry STREQUAL "32_16" AND NOT v EQUAL 128) OR
       (geometry STREQUAL "40_17" AND NOT v EQUAL 64))
      continue()
    endif()
    string(REPLACE "_" ";" fields "${geometry}")
    list(GET fields 0 w)
    list(GET fields 1 b)
    set(name wide_ikea_${w}_${b}_${v})
    add_executable(${name} bench.cpp)
    sixdb_target(${name})
    target_compile_definitions(${name} PRIVATE NDEBUG WIDE_W=${w} WIDE_B=${b} WIDE_V=${v} WIDE_PEERS=0)
    target_link_libraries(${name} PRIVATE ikea::hashtable)
    if(WIDE_LOOM)
      target_compile_definitions(${name} PRIVATE WIDE_LOOM=1)
      target_link_libraries(${name} PRIVATE loom::loom)
    endif()
    target_compile_options(${name} PRIVATE -mllvm -inline-threshold=1000 -falign-functions=64)
    list(APPEND wide_targets ${name})
  endforeach()
  foreach(w 32 40)
    if(w EQUAL 40 AND NOT v EQUAL 64)
      continue()
    endif()
    set(name wide_peers_${w}_${v})
    add_executable(${name} bench.cpp)
    sixdb_target(${name})
    target_compile_definitions(${name} PRIVATE NDEBUG WIDE_W=${w} WIDE_B=9 WIDE_V=${v} WIDE_PEERS=1)
    target_link_libraries(${name} PRIVATE ikea::hashtable hashtable_abseil_baseline wide_f14)
    if(WIDE_LOOM)
      target_compile_definitions(${name} PRIVATE WIDE_LOOM=1)
      target_link_libraries(${name} PRIVATE loom::loom)
    endif()
    target_compile_options(${name} PRIVATE -mllvm -inline-threshold=1000 -falign-functions=64)
    list(APPEND wide_targets ${name})
  endforeach()
endforeach()
add_custom_target(wide_all DEPENDS ${wide_targets})
