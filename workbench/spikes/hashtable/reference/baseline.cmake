include_guard(GLOBAL)
include(FetchContent)

# Verified release 20260817.0. The commit-addressed archive and its hash are the
# same on every worker; FetchContent keeps dependency sources/builds in the
# caller's ignored build tree, never in the spike's source directory.
set(SIXDB_HASHTABLE_ABSEIL_REVISION "2065f4ded0558c6f89fee67c8e5228feb4eb960e")
if(TARGET absl::flat_hash_map)
  message(FATAL_ERROR "hashtable baseline requires its pinned Abseil, not an existing provider")
endif()
if(FETCHCONTENT_SOURCE_DIR_HASHTABLE_ABSEIL)
  message(FATAL_ERROR "hashtable baseline does not accept an unverified Abseil source override")
endif()

set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_BUILD_TEST_HELPERS OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_USE_SYSTEM_INCLUDES ON CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
FetchContent_Declare(hashtable_abseil
  URL "https://codeload.github.com/abseil/abseil-cpp/tar.gz/${SIXDB_HASHTABLE_ABSEIL_REVISION}"
  URL_HASH SHA256=7f4240fe135c0b0dcdd2efa664f1393b1da7e25031e17560515f452182aa0c5e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(hashtable_abseil)

# Keep Abseil's compiled helpers on the same language/ISA/tuning as the benchmark.
# Preserve its own warning and optimization settings. In optimized configurations
# use its release runtime; the benchmark TU must also define NDEBUG for both maps.
function(hashtable_abseil_compile_settings directory)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(kind "${target}" TYPE)
    if(kind MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
      target_compile_features("${target}" PRIVATE cxx_std_23)
      target_compile_definitions("${target}" PRIVATE
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NDEBUG>")
      if(SIXDB_MARCH)
        target_compile_options("${target}" PRIVATE "-march=${SIXDB_MARCH}")
      endif()
      if(sixdb_clang_tune)
        target_compile_options("${target}" PRIVATE "-mtune=${sixdb_clang_tune}")
      endif()
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    hashtable_abseil_compile_settings("${child}")
  endforeach()
endfunction()
hashtable_abseil_compile_settings("${hashtable_abseil_SOURCE_DIR}")

add_library(hashtable_abseil_baseline INTERFACE)
target_compile_features(hashtable_abseil_baseline INTERFACE cxx_std_23)
target_include_directories(hashtable_abseil_baseline INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(hashtable_abseil_baseline INTERFACE absl::flat_hash_map)
