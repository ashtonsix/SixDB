# Keep assembly opt-in and use the same pinned driver/target as C++.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "assembly-calls currently demonstrates the Linux ELF ABI")
endif()
set(CMAKE_ASM_COMPILER "${CMAKE_CXX_COMPILER}")
set(CMAKE_ASM_COMPILER_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
enable_language(ASM)
set(SIXDB_ASSEMBLY_SEED_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/seed.cpp" CACHE FILEPATH
    "C++ seed, or its edited compiler-emitted .s replacement")
add_executable(assembly_calls_check check.cpp bridge.cpp caller.S "${SIXDB_ASSEMBLY_SEED_SOURCE}")
sixdb_target(assembly_calls_check)
target_link_libraries(assembly_calls_check PRIVATE loom::loom ikea::hashtable)
add_test(NAME assembly_calls_check COMMAND assembly_calls_check)

# Compile/inspect real owning-result call shapes independently of directory code.
add_executable(assembly_ownership_check ownership.cpp)
sixdb_target(assembly_ownership_check)
target_link_libraries(assembly_ownership_check PRIVATE loom::loom)
add_test(NAME assembly_ownership_check COMMAND assembly_ownership_check)
