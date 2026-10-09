# Shared input/oracle and LMDB controls; does not enable the allocation consumer.
include_guard(GLOBAL)
find_path(ORBITAL_BENCH_LMDB_INCLUDE lmdb.h REQUIRED)
find_library(ORBITAL_BENCH_LMDB_LIBRARY lmdb REQUIRED)
add_library(orbital_bench_workload STATIC
  "${CMAKE_CURRENT_LIST_DIR}/workload.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/reference.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lmdb.cpp")
sixdb_target(orbital_bench_workload)
target_include_directories(orbital_bench_workload
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
  PRIVATE "${ORBITAL_BENCH_LMDB_INCLUDE}")
target_link_libraries(orbital_bench_workload PUBLIC "${ORBITAL_BENCH_LMDB_LIBRARY}")
