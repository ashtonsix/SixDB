add_executable(frame_encoding_bodies bodies.cpp)
sixdb_target(frame_encoding_bodies)
target_link_libraries(frame_encoding_bodies PRIVATE ikea::seriespack ikea::boundlist)
