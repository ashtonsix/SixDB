# Compatibility for old spike selections; new workload callers select the suite.
include("${PROJECT_SOURCE_DIR}/workbench/benchmarks/seriespack/CMakeLists.txt")
if(NOT TARGET ikea_bec_metadata_adapter AND NOT "bec-packed-metadata" IN_LIST SIXDB_SPIKES)
  add_subdirectory("${PROJECT_SOURCE_DIR}/workbench/spikes/bec-packed-metadata"
    "${PROJECT_BINARY_DIR}/workbench/spikes/bec-packed-metadata")
endif()
