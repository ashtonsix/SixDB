if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  get_filename_component(frame_controls_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
  include("${frame_controls_root}/cmake/ClangVersion.cmake")
  find_program(frame_controls_clang NAMES clang++-${SIXDB_CLANG_MAJOR} REQUIRED)
  set(CMAKE_CXX_COMPILER "${frame_controls_clang}" CACHE FILEPATH "Pinned compiler")
  project(FrameNativeControls LANGUAGES CXX)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  add_subdirectory("${frame_controls_root}" sixdb)
endif()

get_filename_component(frame_controls_cells "${CMAKE_CURRENT_SOURCE_DIR}/../../frame-phase-2/instruments/cells" ABSOLUTE)
if(NOT TARGET frame_cells_controls)
  # Same reusable control source, without enabling the old image runner.
  add_library(frame_cells_controls STATIC EXCLUDE_FROM_ALL "${frame_controls_cells}/kernels.cpp")
  sixdb_target(frame_cells_controls)
  target_include_directories(frame_cells_controls PUBLIC "${frame_controls_cells}"
    "${frame_controls_cells}/../../substrate" "${frame_controls_cells}/../../bodies")
  target_link_libraries(frame_cells_controls PUBLIC ikea::intervals)
endif()

add_executable(frame3_native_controls_check EXCLUDE_FROM_ALL check.cpp)
sixdb_target(frame3_native_controls_check)
target_link_libraries(frame3_native_controls_check PRIVATE frame_cells_controls)

add_executable(frame3_native_controls_sanitize EXCLUDE_FROM_ALL check.cpp "${frame_controls_cells}/kernels.cpp")
sixdb_target(frame3_native_controls_sanitize)
target_include_directories(frame3_native_controls_sanitize PRIVATE "${frame_controls_cells}"
  "${frame_controls_cells}/../../substrate" "${frame_controls_cells}/../../bodies")
target_link_libraries(frame3_native_controls_sanitize PRIVATE ikea::intervals)
target_compile_options(frame3_native_controls_sanitize PRIVATE
  -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(frame3_native_controls_sanitize PRIVATE -fsanitize=address,undefined)
