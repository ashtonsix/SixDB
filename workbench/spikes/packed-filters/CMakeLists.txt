add_executable(packed_filters_bench bench.cpp op_0.cpp op_1.cpp op_2.cpp op_3.cpp op_4.cpp)
sixdb_target(packed_filters_bench)
target_compile_options(packed_filters_bench PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_bench PRIVATE ikea::seriespack ikea::tuplepack)

add_executable(packed_filters_early early-main.cpp early-a.cpp early-b.cpp early-c.cpp early-d.cpp
  early-tuple1.cpp early-tuple3.cpp early-tuple5.cpp early-tuple8.cpp
  early-tuple-1-0.cpp early-tuple-1-1.cpp early-tuple-1-2.cpp early-tuple-1-3.cpp early-tuple-1-4.cpp
  early-tuple-3-0.cpp early-tuple-3-1.cpp early-tuple-3-2.cpp early-tuple-3-3.cpp early-tuple-3-4.cpp
  early-tuple-5-0.cpp early-tuple-5-1.cpp early-tuple-5-2.cpp early-tuple-5-3.cpp early-tuple-5-4.cpp
  early-tuple-8-0.cpp early-tuple-8-1.cpp early-tuple-8-2.cpp early-tuple-8-3.cpp early-tuple-8-4.cpp)
sixdb_target(packed_filters_early)
target_compile_options(packed_filters_early PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_early PRIVATE ikea::seriespack ikea::tuplepack)

add_executable(packed_filters_early_guards early-guards.cpp)
sixdb_target(packed_filters_early_guards)
target_compile_options(packed_filters_early_guards PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_early_guards PRIVATE ikea::seriespack)

add_executable(packed_filters_early_wide early-main.cpp early-wide.cpp)
sixdb_target(packed_filters_early_wide)
target_compile_definitions(packed_filters_early_wide PRIVATE EARLY_NO_MAIN=1)
target_compile_options(packed_filters_early_wide PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_early_wide PRIVATE ikea::seriespack)

add_executable(packed_filters_early_zero early-main.cpp early-zero.cpp)
sixdb_target(packed_filters_early_zero)
target_compile_definitions(packed_filters_early_zero PRIVATE EARLY_NO_MAIN=1)
target_compile_options(packed_filters_early_zero PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_early_zero PRIVATE ikea::seriespack)

add_executable(packed_filters_early_graduate early-main.cpp early-graduate.cpp)
sixdb_target(packed_filters_early_graduate)
target_compile_definitions(packed_filters_early_graduate PRIVATE EARLY_NO_MAIN=1)
target_compile_options(packed_filters_early_graduate PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_early_graduate PRIVATE ikea::seriespack)
