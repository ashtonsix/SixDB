add_library(ikea2_predecessor STATIC EXCLUDE_FROM_ALL
  src/seriespack/view.cpp src/seriespack/operations.cpp src/seriespack/native.cpp)
sixdb_target(ikea2_predecessor)
target_include_directories(ikea2_predecessor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
