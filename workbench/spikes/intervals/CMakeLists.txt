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

# The namespace migration is complete. Exercise the actual owner, not two
# generated copies of today's identical implementation labeled before/after.
add_executable(intervals_directory_check ALIAS engine_directory_check)
add_executable(intervals_directory_reads
  "${PROJECT_SOURCE_DIR}/workbench/spikes/segment-directory/engine_reads.cpp"
  "${PROJECT_SOURCE_DIR}/workbench/spikes/segment-directory/layer.cpp")
sixdb_target(intervals_directory_reads)
target_link_libraries(intervals_directory_reads PRIVATE engine::directory)
add_executable(intervals_consumer_edits consumer_edits.cpp)
sixdb_target(intervals_consumer_edits)
target_compile_options(intervals_consumer_edits PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(intervals_consumer_edits PRIVATE ikea::intervals ikea::streampack)

foreach(check intervals_check intervals_seam_check intervals_directory_check)
  add_test(NAME ${check} COMMAND $<TARGET_FILE:${check}>)
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

option(SIXDB_INTERVALS_FRAME_RUN_AUDIT "Compare run projection in actual Frame point readers" OFF)
if(SIXDB_INTERVALS_FRAME_RUN_AUDIT)
  if(NOT TARGET frame_encoding_material OR NOT TARGET frame_point_kernels)
    message(FATAL_ERROR "Frame caller audit requires -DSIXDB_SPIKES=ikea-frame;intervals in that order")
  endif()
  set(frame_source "${PROJECT_SOURCE_DIR}/workbench/spikes/ikea-frame")
  set(frame_generated "${CMAKE_CURRENT_BINARY_DIR}/frame-runs")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${frame_source}/point/point_read.h" "${frame_source}/point/compact_read.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/frame-runs-generate.py")
  execute_process(COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/frame-runs-generate.py"
    "${frame_source}" "${frame_generated}" COMMAND_ERROR_IS_FATAL ANY)
  add_executable(intervals_frame_runs frame-runs.cpp)
  sixdb_target(intervals_frame_runs)
  target_include_directories(intervals_frame_runs PRIVATE "${frame_source}/point" "${frame_generated}")
  target_compile_options(intervals_frame_runs PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_link_libraries(intervals_frame_runs PRIVATE frame_encoding_material frame_point_kernels ikea::intervals)
endif()
