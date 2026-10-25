cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build, e.g. cmake --preset dev")
endif()

# CMake may create its initial control files before reading this file. Verify
# the output boundary before compiler detection or larger build intermediates.
# Disposable CMake-only fixtures need not copy the Python tooling.
if(EXISTS "${CMAKE_SOURCE_DIR}/workbench/tools/storage.py")
  find_program(SIXDB_STORAGE_PYTHON NAMES python3 REQUIRED)
  execute_process(
    COMMAND "${SIXDB_STORAGE_PYTHON}" -B "${CMAKE_SOURCE_DIR}/workbench/tools/storage.py"
      --check-build "${CMAKE_BINARY_DIR}"
    RESULT_VARIABLE SIXDB_STORAGE_CHECK_RESULT
    ERROR_VARIABLE SIXDB_STORAGE_CHECK_ERROR
    OUTPUT_QUIET)
  if(NOT SIXDB_STORAGE_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "${SIXDB_STORAGE_CHECK_ERROR}")
  endif()
endif()

# Keep compiler/linker intermediates with the build, including when Ninja is
# invoked later from an ordinary shell without the Python helper environment.
set(SIXDB_BUILD_TEMP "${CMAKE_BINARY_DIR}/.tmp")
file(MAKE_DIRECTORY "${SIXDB_BUILD_TEMP}")
foreach(name TMPDIR TMP TEMP)
  set(ENV{${name}} "${SIXDB_BUILD_TEMP}")
endforeach()
foreach(language C CXX)
  foreach(kind COMPILER LINKER)
    list(PREPEND CMAKE_${language}_${kind}_LAUNCHER
      "${CMAKE_COMMAND}" -E env
      "TMPDIR=${SIXDB_BUILD_TEMP}" "TMP=${SIXDB_BUILD_TEMP}" "TEMP=${SIXDB_BUILD_TEMP}")
  endforeach()
endforeach()

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
enable_testing()

# Target dependencies are declared by their owners; order defines no edges.
foreach(module ikea orbital loom engine shore workbench)
  add_subdirectory(${module})
endforeach()
