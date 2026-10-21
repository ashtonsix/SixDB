if(PRIMARY_HINT_BENCH)
  foreach(v 0 8)
    add_executable(primary_hint_stream_bench_${v} hint-stream-bench.cpp)
    sixdb_target(primary_hint_stream_bench_${v})
    target_link_libraries(primary_hint_stream_bench_${v} PRIVATE ikea::hashtable workbench::pmu)
    target_include_directories(primary_hint_stream_bench_${v} BEFORE PRIVATE "${MEMORY_INCLUDE_OVERRIDE}")
    target_compile_definitions(primary_hint_stream_bench_${v} PRIVATE PRIMARY_HINT_BENCH_FILE="${MEMORY_BENCH_SOURCE}"
      NDEBUG MEMORY_W=32 OVERFLOW_B=11 OVERFLOW_V=${v} OVERFLOW_NAME="ikea" OVERFLOW_PEERS=0
      OVERFLOW_SWISS_CEILING32=28 SWISS_GROUP_BYTES=16)
    target_compile_options(primary_hint_stream_bench_${v} PRIVATE -mllvm -inline-threshold=1000 -falign-functions=64)
  endforeach()
endif()
