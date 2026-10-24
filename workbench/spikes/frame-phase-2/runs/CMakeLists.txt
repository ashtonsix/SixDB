# Run boundaries as composed stages: Intervals geometry, the bounds stage and
# SeriesPack packing (Ashton's composition, 2026-10-24).
add_executable(frame_runs_census census.cpp)
sixdb_target(frame_runs_census)
target_link_libraries(frame_runs_census PRIVATE ikea::seriespack)
add_executable(frame_runs_read read.cpp)
sixdb_target(frame_runs_read)
target_link_libraries(frame_runs_read PRIVATE ikea::seriespack ikea::intervals)
