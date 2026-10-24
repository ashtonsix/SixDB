add_executable(bitset_advisor_trace trace.cpp)
sixdb_target(bitset_advisor_trace)
target_link_libraries(bitset_advisor_trace PRIVATE ikea::bec)
