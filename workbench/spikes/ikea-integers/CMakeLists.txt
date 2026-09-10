add_library(ikea_integer_kernels STATIC codec.cpp reference.cpp)
sixdb_target(ikea_integer_kernels)

add_executable(ikea_integer_check check.cpp)
sixdb_target(ikea_integer_check)
target_link_libraries(ikea_integer_check PRIVATE ikea_integer_kernels)

add_executable(ikea_integer_pairs_bench pairs_bench.cpp)
sixdb_target(ikea_integer_pairs_bench)

add_subdirectory(composition)

if(IKEA_INTEGER_PRIOR_DIR)
  add_library(ikea_integer_prior STATIC prior.cpp)
  sixdb_target(ikea_integer_prior)
  target_include_directories(ikea_integer_prior SYSTEM PRIVATE "${IKEA_INTEGER_PRIOR_DIR}")
  if(SIXDB_TUNE STREQUAL "zen5")
    target_compile_definitions(ikea_integer_prior PRIVATE BP_CPU=zen5)
  elseif(SIXDB_TUNE STREQUAL "granite-rapids")
    target_compile_definitions(ikea_integer_prior PRIVATE BP_CPU=granite_rapids)
  elseif(SIXDB_TUNE STREQUAL "neoverse-v2")
    target_compile_definitions(ikea_integer_prior PRIVATE BP_CPU=neoverse_v2)
  endif()
  target_compile_definitions(ikea_integer_check PRIVATE IP_HAS_PRIOR=1)
  target_link_libraries(ikea_integer_check PRIVATE ikea_integer_prior)

  add_executable(ikea_integer_bench bench.cpp)
  sixdb_target(ikea_integer_bench)
  target_compile_definitions(ikea_integer_bench PRIVATE IP_HAS_PRIOR=1)
  target_link_libraries(ikea_integer_bench PRIVATE ikea_integer_kernels ikea_integer_prior)
endif()

add_subdirectory(wide56)
