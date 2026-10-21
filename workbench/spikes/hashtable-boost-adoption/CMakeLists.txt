include("${PROJECT_SOURCE_DIR}/workbench/benchmarks/hashtable/abseil.cmake")
include(FetchContent)
FetchContent_Declare(memory_boost
  URL https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2
  URL_HASH SHA256=5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-cmake-needed)
FetchContent_MakeAvailable(memory_boost)

set(MEMORY_CASES "24_10;24_11;24_12;24_13;32_10;32_11;32_12;40_10;40_11;40_12;40_20;40_21;40_22;50_20;50_21;50_22"
  CACHE STRING "W_b concrete geometries")
set(MEMORY_WIDTHS "24;32;40;50" CACHE STRING "Peer key domains")
set(MEMORY_VALUES "0;8" CACHE STRING "Value widths")
set(MEMORY_INCLUDE_OVERRIDE "" CACHE PATH "Research-only header overlay")
set(memory_targets)
foreach(case IN LISTS MEMORY_CASES)
  string(REPLACE "_" ";" fields "${case}")
  list(GET fields 0 w)
  list(GET fields 1 b)
  foreach(v IN LISTS MEMORY_VALUES)
    set(target memory_ikea_${w}_${b}_${v})
    add_executable(${target} memory-bench.cpp)
    sixdb_target(${target})
    target_link_libraries(${target} PRIVATE ikea::hashtable workbench::pmu)
    if(MEMORY_INCLUDE_OVERRIDE)
      target_include_directories(${target} BEFORE PRIVATE "${MEMORY_INCLUDE_OVERRIDE}")
    endif()
    target_compile_definitions(${target} PRIVATE NDEBUG MEMORY_W=${w} OVERFLOW_B=${b}
      OVERFLOW_V=${v} OVERFLOW_NAME="ikea" OVERFLOW_PEERS=0
      OVERFLOW_SWISS_CEILING32=28 SWISS_GROUP_BYTES=16)
    target_compile_options(${target} PRIVATE -mllvm -inline-threshold=1000 -falign-functions=64)
    list(APPEND memory_targets ${target})
  endforeach()
endforeach()
foreach(w IN LISTS MEMORY_WIDTHS)
  foreach(v IN LISTS MEMORY_VALUES)
    set(target memory_peers_${w}_${v})
    add_executable(${target} memory-bench.cpp)
    sixdb_target(${target})
    target_link_libraries(${target} PRIVATE ikea::hashtable hashtable_abseil_baseline workbench::pmu)
    target_include_directories(${target} SYSTEM PRIVATE "${memory_boost_SOURCE_DIR}")
    target_compile_definitions(${target} PRIVATE NDEBUG MEMORY_W=${w} OVERFLOW_B=10
      OVERFLOW_V=${v} OVERFLOW_NAME="peers" OVERFLOW_PEERS=1
      OVERFLOW_SWISS_CEILING32=28 SWISS_GROUP_BYTES=16)
    target_compile_options(${target} PRIVATE -mllvm -inline-threshold=1000 -falign-functions=64)
    list(APPEND memory_targets ${target})
  endforeach()
endforeach()
add_custom_target(memory_all DEPENDS ${memory_targets})
