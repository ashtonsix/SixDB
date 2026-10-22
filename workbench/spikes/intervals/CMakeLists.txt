add_executable(intervals_check ALIAS ikea_intervals_sparse_check)
foreach(program consumer)
  add_executable(intervals_${program} ${program}.cpp)
  sixdb_target(intervals_${program})
  target_compile_options(intervals_${program} PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_link_libraries(intervals_${program} PRIVATE ikea::intervals ikea::streampack)
endforeach()
add_executable(intervals_seam_check ALIAS ikea_intervals_seams_check)
add_executable(intervals_consumer_seams consumer_seams.cpp)
sixdb_target(intervals_consumer_seams)
target_compile_options(intervals_consumer_seams PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(intervals_consumer_seams PRIVATE ikea::intervals ikea::streampack)

# Compile the actual Directory implementation against each namespace boundary.
# Only the include and namespace binding change; its Object/Part/material work is
# deliberately identical. Generated source lives in the ignored build tree.
set(directory_source "${PROJECT_SOURCE_DIR}/engine/src/directory.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${directory_source}")
file(READ "${directory_source}" intervals_directory_source)
if(NOT intervals_directory_source MATCHES "namespace bl=ikea::(boundlist|intervals);")
  message(FATAL_ERROR "Directory source binding changed; review the Intervals comparison transform")
endif()
string(REPLACE "#include <ikea/boundlist.h>" "#include <ikea/intervals.h>"
  intervals_directory_source "${intervals_directory_source}")
string(REPLACE "namespace bl=ikea::boundlist;" "namespace bl=ikea::intervals;"
  intervals_directory_source "${intervals_directory_source}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/directory_intervals.cpp" "${intervals_directory_source}")
add_library(intervals_directory STATIC "${CMAKE_CURRENT_BINARY_DIR}/directory_intervals.cpp"
  "${PROJECT_SOURCE_DIR}/engine/src/directory_material.cpp")
sixdb_target(intervals_directory)
target_include_directories(intervals_directory PUBLIC "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(intervals_directory PUBLIC orbital::orbital loom::loom PRIVATE ikea::intervals)
add_executable(intervals_directory_check "${PROJECT_SOURCE_DIR}/engine/test/directory.cpp")
sixdb_target(intervals_directory_check)
target_link_libraries(intervals_directory_check PRIVATE intervals_directory)
foreach(variant before after)
  add_executable(intervals_directory_${variant}
    "${PROJECT_SOURCE_DIR}/workbench/spikes/segment-directory/engine_reads.cpp"
    "${PROJECT_SOURCE_DIR}/workbench/spikes/segment-directory/layer.cpp")
  sixdb_target(intervals_directory_${variant})
endforeach()
target_link_libraries(intervals_directory_before PRIVATE engine::directory)
target_link_libraries(intervals_directory_after PRIVATE intervals_directory)
add_executable(intervals_consumer_edits consumer_edits.cpp)
sixdb_target(intervals_consumer_edits)
target_compile_options(intervals_consumer_edits PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(intervals_consumer_edits PRIVATE ikea::intervals ikea::streampack)

foreach(check intervals_check intervals_seam_check intervals_directory_check)
  add_test(NAME ${check} COMMAND ${check})
endforeach()
add_custom_target(intervals_validate
  COMMAND "$<TARGET_FILE:intervals_check>"
  COMMAND "$<TARGET_FILE:intervals_seam_check>"
  COMMAND "$<TARGET_FILE:intervals_directory_check>"
  COMMAND "$<TARGET_FILE:ikea_streampack_check>"
  COMMAND "$<TARGET_FILE:ikea_streampack_boundaries_check>"
  DEPENDS intervals_check intervals_seam_check intervals_directory_check
          ikea_streampack_check ikea_streampack_boundaries_check
  USES_TERMINAL)
add_executable(intervals_latency latency.cpp)
sixdb_target(intervals_latency)
target_compile_options(intervals_latency PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(intervals_latency PRIVATE ikea::intervals ikea::streampack)
