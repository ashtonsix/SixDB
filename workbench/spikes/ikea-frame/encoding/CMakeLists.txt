# Private experimental material shared by the driver, Codex's access check and
# Engine's consumers. Codecs compile once here; no public Frame API.
add_library(frame_encoding_material STATIC material.cpp kernels.cpp)
sixdb_target(frame_encoding_material)
target_include_directories(frame_encoding_material PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frame_encoding_material PUBLIC ikea::seriespack ikea::intervals)

add_executable(frame_encoding_bodies bodies.cpp)
sixdb_target(frame_encoding_bodies)
target_link_libraries(frame_encoding_bodies PRIVATE frame_encoding_material)

# Codex's protected-payload correctness fixture.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/masked_access_check.cpp)
    add_executable(frame_encoding_masked_access_check masked_access_check.cpp)
    sixdb_target(frame_encoding_masked_access_check)
    target_link_libraries(frame_encoding_masked_access_check PRIVATE frame_encoding_material)
endif()
