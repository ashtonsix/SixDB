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

# Full variable/type DWARF roughly doubles compile time and memory for the
# template-heavy checks; line tables keep stack traces and profiles. Release
# keeps the full -g used for retained crash symbols.
set(SIXDB_DEBUG_INFO "lines" CACHE STRING "RelWithDebInfo debug info: lines or full")
set_property(CACHE SIXDB_DEBUG_INFO PROPERTY STRINGS lines full)
if(SIXDB_DEBUG_INFO STREQUAL "lines")
  target_compile_options(sixdb_build_options INTERFACE
    $<$<CONFIG:RelWithDebInfo>:-gline-tables-only>)
elseif(NOT SIXDB_DEBUG_INFO STREQUAL "full")
  message(FATAL_ERROR "SIXDB_DEBUG_INFO must be lines or full")
endif()

# Width-matrix checks compile every SeriesPack width under full scope; fast
# samples boundary widths for quicker edit/test loops (ikea/test/scope.h).
set(SIXDB_TEST_SCOPE "fast" CACHE STRING "Width-matrix test scope: fast or full")
set_property(CACHE SIXDB_TEST_SCOPE PROPERTY STRINGS fast full)
if(NOT SIXDB_TEST_SCOPE MATCHES "^(fast|full)$")
  message(FATAL_ERROR "SIXDB_TEST_SCOPE must be fast or full")
endif()
function(sixdb_test_scope)
  if(SIXDB_TEST_SCOPE STREQUAL "full")
    foreach(target IN LISTS ARGN)
      target_compile_definitions(${target} PRIVATE IKEA_TEST_SCOPE_FULL)
    endforeach()
  endif()
endfunction()

# Some test TUs peak near 1.5 GiB (lines) or 3 GiB (full). Ninja runs their
# targets in a memory-sized pool, leaving headroom for the ordinary -j jobs, so
# the default job count cannot swap or OOM the host.
set(SIXDB_HEAVY_COMPILE_JOBS 0 CACHE STRING "Parallel heavy compiles; 0 sizes from host memory")
if(SIXDB_HEAVY_COMPILE_JOBS)
  set(sixdb_heavy_jobs ${SIXDB_HEAVY_COMPILE_JOBS})
else()
  cmake_host_system_information(RESULT sixdb_memory_mib QUERY TOTAL_PHYSICAL_MEMORY)
  if(SIXDB_DEBUG_INFO STREQUAL "lines")
    math(EXPR sixdb_heavy_jobs "${sixdb_memory_mib} / 1792")
  else()
    math(EXPR sixdb_heavy_jobs "${sixdb_memory_mib} / 3072")
  endif()
  if(sixdb_heavy_jobs LESS 1)
    set(sixdb_heavy_jobs 1)
  endif()
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS sixdb_heavy_compile=${sixdb_heavy_jobs})

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

# Mark targets whose TUs approach the heavy-compile memory budget.
function(sixdb_heavy_compile)
  set_target_properties(${ARGN} PROPERTIES JOB_POOL_COMPILE sixdb_heavy_compile)
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
