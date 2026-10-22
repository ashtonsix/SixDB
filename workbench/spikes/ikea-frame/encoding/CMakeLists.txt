add_executable(frame_encoding_bodies bodies.cpp)
sixdb_target(frame_encoding_bodies)
target_link_libraries(frame_encoding_bodies PRIVATE ikea::seriespack ikea::boundlist)

# Codex's protected-payload correctness fixture reuses bodies.cpp privately
# (compiled with FRAME_ENCODING_BODY_NO_MAIN); it builds once the source lands.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/masked_access_check.cpp)
    add_executable(frame_encoding_masked_access_check masked_access_check.cpp)
    sixdb_target(frame_encoding_masked_access_check)
    target_link_libraries(frame_encoding_masked_access_check PRIVATE ikea::seriespack ikea::boundlist)
endif()
