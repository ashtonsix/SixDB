add_executable(filter_composition_runtime runtime.cpp
  widths_1.cpp widths_9.cpp widths_17.cpp widths_25.cpp
  widths_33.cpp widths_41.cpp widths_49.cpp widths_57.cpp)
sixdb_target(filter_composition_runtime)
target_compile_options(filter_composition_runtime PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(filter_composition_runtime PRIVATE ikea::seriespack)

add_executable(filter_composition_extra runtime.cpp extra.cpp extra_pair_0.cpp extra_pair_1.cpp
  extra_tuple_0.cpp extra_tuple_1.cpp extra_tuple_2.cpp extra_tuple_3.cpp extra_tuple_4.cpp)
sixdb_target(filter_composition_extra)
target_compile_definitions(filter_composition_extra PRIVATE FILTER_SEAMS_NO_MAIN=1)
target_compile_options(filter_composition_extra PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(filter_composition_extra PRIVATE ikea::seriespack ikea::tuplepack)

add_executable(filter_composition_recheck runtime.cpp recheck.cpp)
sixdb_target(filter_composition_recheck)
target_compile_definitions(filter_composition_recheck PRIVATE FILTER_SEAMS_NO_MAIN=1)
target_compile_options(filter_composition_recheck PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(filter_composition_recheck PRIVATE ikea::seriespack)

add_executable(filter_composition_sanity EXCLUDE_FROM_ALL runtime.cpp extra.cpp sanity.cpp)
sixdb_target(filter_composition_sanity)
target_compile_definitions(filter_composition_sanity PRIVATE FILTER_SEAMS_NO_MAIN=1 FILTER_EXTRAS_NO_MAIN=1)
target_link_libraries(filter_composition_sanity PRIVATE ikea::seriespack ikea::tuplepack)
