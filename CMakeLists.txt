cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build, e.g. cmake --preset dev")
endif()

include(cmake/ClangVersion.cmake)
if(NOT DEFINED CMAKE_CXX_COMPILER)
  find_program(SIXDB_CLANGXX NAMES clang++-${SIXDB_CLANG_MAJOR} clang++ REQUIRED)
  set(CMAKE_CXX_COMPILER "${SIXDB_CLANGXX}" CACHE FILEPATH "Pinned SixDB compiler")
endif()

# Assertions stay enabled in both optimized configurations. Further diagnostic
# and measurement variants can be added explicitly when needed.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "Development flags")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g" CACHE STRING "Release flags")
project(SixDB LANGUAGES CXX)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL SIXDB_CLANG_VERSION)
  message(FATAL_ERROR
    "SixDB requires Clang ${SIXDB_CLANG_VERSION}; found "
    "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
    "Select the pinned compiler in a fresh build directory.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build configuration" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/Targets.cmake)

# Target dependencies are declared by their owners; order defines no edges.
foreach(module ikea orbital loom engine shore workbench)
  add_subdirectory(${module})
endforeach()

# Experimental replacement; existing module consumers keep using ikea.
add_subdirectory(ikea2 EXCLUDE_FROM_ALL)
