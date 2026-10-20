include(workload.cmake)

add_executable(orbital_request_failure_check request_failure.cpp)
sixdb_target(orbital_request_failure_check)
target_include_directories(orbital_request_failure_check PRIVATE "${ORBITAL_BENCH_LMDB_INCLUDE}")
target_link_libraries(orbital_request_failure_check PRIVATE orbital_bench_workload)

add_library(orbital_bench_packed STATIC packed.cpp packed-page.cpp lookup.cpp packed-construction.cpp inventory.cpp)
sixdb_target(orbital_bench_packed)
target_link_libraries(orbital_bench_packed PUBLIC orbital_bench_workload loom::loom ikea::seriespack ikea::tuplepack)

add_executable(orbital_allocation_bench main.cpp)
sixdb_target(orbital_allocation_bench)
target_link_libraries(orbital_allocation_bench PRIVATE orbital_bench_packed workbench::pmu)
add_executable(orbital_workload_check check.cpp)
sixdb_target(orbital_workload_check)
target_link_libraries(orbital_workload_check PRIVATE orbital_bench_packed)
add_executable(orbital_split_check split.cpp)
sixdb_target(orbital_split_check)
target_link_libraries(orbital_split_check PRIVATE orbital_bench_packed)
add_executable(orbital_startup_check startup.cpp)
sixdb_target(orbital_startup_check)
target_link_libraries(orbital_startup_check PRIVATE orbital_bench_packed)
target_link_options(orbital_startup_check PRIVATE
  "LINKER:--wrap=_ZN7orbital10allocation8add_poolERNS0_5ArenaERNS_6worker4LaneE")
add_executable(orbital_pack_growth_check growth.cpp)
sixdb_target(orbital_pack_growth_check)
target_link_libraries(orbital_pack_growth_check PRIVATE loom::loom ikea::seriespack ikea::tuplepack)
add_executable(orbital_lifecycle_bench pressure.cpp)
sixdb_target(orbital_lifecycle_bench)
target_link_libraries(orbital_lifecycle_bench PRIVATE loom::loom ikea::seriespack ikea::tuplepack)

foreach(check slot_maintenance construction read_bounds)
  add_executable(orbital_${check}_check ${check}.cpp)
  sixdb_target(orbital_${check}_check)
  target_link_libraries(orbital_${check}_check PRIVATE orbital_bench_packed)
endforeach()
add_executable(orbital_inventory_check inventory_check.cpp)
sixdb_target(orbital_inventory_check)
target_link_libraries(orbital_inventory_check PRIVATE orbital_bench_packed)
target_link_options(orbital_inventory_check PRIVATE
  "LINKER:--wrap=_ZN13orbital_bench6packed20construct_blank_pageERNS0_4PageERKNS0_6SchemaESt4spanISt4byteLm18446744073709551615EEmb"
  "LINKER:--wrap=_ZN7orbital10allocation10open_arenaEmi"
  "LINKER:--wrap=clock_gettime")
