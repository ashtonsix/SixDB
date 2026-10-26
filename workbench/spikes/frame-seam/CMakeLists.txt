add_executable(frame_seam_measure EXCLUDE_FROM_ALL seam.cpp)
sixdb_target(frame_seam_measure)
target_link_libraries(frame_seam_measure PRIVATE ikea::frame)

add_executable(frame_seam_span_compare EXCLUDE_FROM_ALL span_compare.cpp)
sixdb_target(frame_seam_span_compare)
target_compile_definitions(frame_seam_span_compare PRIVATE FRAME_SPAN_INTERFACE=1)
target_link_libraries(frame_seam_span_compare PRIVATE ikea::frame)

# A source capture may supply frozen Frame controls for paired qualification.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native-controls.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/native-controls.cmake")
endif()
