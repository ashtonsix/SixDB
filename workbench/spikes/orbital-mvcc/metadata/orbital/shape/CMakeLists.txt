add_library(orbital_mvcc_shape STATIC shape.cpp)
sixdb_target(orbital_mvcc_shape)
target_include_directories(orbital_mvcc_shape PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/workbench/spikes/orbital-mvcc")
target_link_libraries(orbital_mvcc_shape PUBLIC orbital_versioned_data orbital::orbital)
