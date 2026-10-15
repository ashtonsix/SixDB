add_executable(packed_filters_bench bench.cpp op_0.cpp op_1.cpp op_2.cpp op_3.cpp op_4.cpp)
sixdb_target(packed_filters_bench)
target_compile_options(packed_filters_bench PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(packed_filters_bench PRIVATE ikea::seriespack ikea::tuplepack)
