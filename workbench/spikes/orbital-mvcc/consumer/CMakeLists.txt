add_library(orbital_object_trace STATIC trace.cpp)
sixdb_target(orbital_object_trace)
target_include_directories(orbital_object_trace PUBLIC .)

add_library(orbital_versioned_data STATIC data.cpp)
sixdb_target(orbital_versioned_data)
target_link_libraries(orbital_versioned_data PUBLIC orbital_object_trace orbital::orbital ikea::seriespack ikea::tuplepack)
target_include_directories(orbital_versioned_data PUBLIC . "${CMAKE_CURRENT_SOURCE_DIR}/..")
