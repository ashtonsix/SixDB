add_executable(segment_directory main.cpp layer.cpp)
sixdb_target(segment_directory)
add_test(NAME segment_directory_check COMMAND segment_directory --check)
