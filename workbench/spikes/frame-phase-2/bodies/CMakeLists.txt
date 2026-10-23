# Front C: integer body leaves. Needs frame-phase-1 configured first (its
# SeriesPack packing and catalogue are the byte and leaf references).
add_library(frame_bodies STATIC catalogue.cpp catalogue_local8.cpp catalogue_plane64.cpp catalogue_plane256.cpp)
sixdb_target(frame_bodies)
target_include_directories(frame_bodies PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(frame_bodies_check check.cpp)
sixdb_target(frame_bodies_check)
target_link_libraries(frame_bodies_check PRIVATE frame_bodies frame_encoding_material)

add_executable(frame_bodies_measure measure.cpp)
sixdb_target(frame_bodies_measure)
target_link_libraries(frame_bodies_measure PRIVATE frame_bodies frame_encoding_material)

add_executable(frame_bodies_columns columns.cpp)
sixdb_target(frame_bodies_columns)
target_link_libraries(frame_bodies_columns PRIVATE frame_bodies)
