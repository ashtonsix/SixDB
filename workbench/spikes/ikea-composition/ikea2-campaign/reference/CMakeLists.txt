add_library(ikea_seriespack_predecessor STATIC EXCLUDE_FROM_ALL
  src/seriespack/view.cpp src/seriespack/operations.cpp src/seriespack/native.cpp)
sixdb_target(ikea_seriespack_predecessor)
target_include_directories(ikea_seriespack_predecessor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
