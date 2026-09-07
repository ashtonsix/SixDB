set(SIXDB_MARCH "" CACHE STRING "Explicit compiler -march value; empty uses the target baseline")
option(SIXDB_TIME_TRACE "Emit Clang compilation time traces" OFF)

add_library(sixdb_build_options INTERFACE)
add_library(sixdb::build_options ALIAS sixdb_build_options)
target_compile_features(sixdb_build_options INTERFACE cxx_std_23)
target_compile_options(sixdb_build_options INTERFACE
  -Wall -Wextra -Wpedantic
  -fno-fast-math -ffp-contract=off -fdenormal-fp-math=ieee
  "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.")
# Also prevent a global fast-math driver flag from injecting crtfastmath.o.
target_link_options(sixdb_build_options INTERFACE -fno-fast-math)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_options(sixdb_build_options INTERFACE -ffunction-sections -fdata-sections)
  target_link_options(sixdb_build_options INTERFACE -Wl,--gc-sections)
endif()
if(SIXDB_MARCH)
  target_compile_options(sixdb_build_options INTERFACE "-march=${SIXDB_MARCH}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/Tuning.cmake")
if(SIXDB_TIME_TRACE)
  target_compile_options(sixdb_build_options INTERFACE -ftime-trace)
endif()

# Ordinary CMake declares sources and dependencies. This helper only applies
# project settings to a compiled first-party target, keeping vendor targets free
# to use their own settings. Share compiled STATIC libraries between consumers.
function(sixdb_target target)
  target_link_libraries(${target} PRIVATE sixdb::build_options)
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    UNITY_BUILD OFF
    INTERPROCEDURAL_OPTIMIZATION FALSE)
endfunction()

# Explicit packaging keeps the normal binary useful for debugging/profiling.
# Distribute dist/bin; retain dist/symbols privately for crash diagnosis.
function(sixdb_release_artifact target)
  get_target_property(kind ${target} TYPE)
  if(NOT kind STREQUAL "EXECUTABLE" OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "sixdb_release_artifact currently supports Linux executables")
  endif()
  find_program(SIXDB_OBJCOPY NAMES llvm-objcopy-${SIXDB_CLANG_MAJOR} REQUIRED)
  find_program(SIXDB_STRIP NAMES llvm-strip-${SIXDB_CLANG_MAJOR} REQUIRED)
  set(binary "${PROJECT_BINARY_DIR}/dist/bin/${target}")
  set(symbols "${PROJECT_BINARY_DIR}/dist/symbols/${target}.debug")
  set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PackageExecutable.cmake")
  add_custom_command(OUTPUT "${binary}" "${symbols}"
    COMMAND "${CMAKE_COMMAND}"
      "-DINPUT=$<TARGET_FILE:${target}>" "-DBINARY=${binary}" "-DSYMBOLS=${symbols}"
      "-DCONFIG=$<CONFIG>" "-DOBJCOPY=${SIXDB_OBJCOPY}" "-DSTRIP=${SIXDB_STRIP}"
      -P "${script}"
    DEPENDS ${target} "${script}"
    VERBATIM)
  add_custom_target(${target}_dist DEPENDS "${binary}" "${symbols}")
endfunction()
