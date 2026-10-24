# SeriesPack remeasured through its maintained reader and writer, beside the
# native array and front C's plane256 image (the substrate's image.h).
add_executable(frame_seriespack_measure measure.cpp)
sixdb_target(frame_seriespack_measure)
target_link_libraries(frame_seriespack_measure PRIVATE ikea::seriespack frame_bodies frame_cells_controls ikea::intervals)
