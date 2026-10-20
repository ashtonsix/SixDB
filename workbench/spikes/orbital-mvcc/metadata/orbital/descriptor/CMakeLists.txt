add_library(orbital_mvcc_descriptor STATIC descriptor.cpp)
sixdb_target(orbital_mvcc_descriptor)
target_include_directories(orbital_mvcc_descriptor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(orbital_mvcc_descriptor PUBLIC orbital::orbital)
