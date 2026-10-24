find_package(Threads REQUIRED)
add_executable(frame_bandwidth_probe bandwidth.cpp bandwidth_kernels.cpp)
sixdb_target(frame_bandwidth_probe)
target_link_libraries(frame_bandwidth_probe PRIVATE Threads::Threads)

add_executable(frame_bandwidth_check EXCLUDE_FROM_ALL bandwidth.cpp bandwidth_kernels.cpp)
sixdb_target(frame_bandwidth_check)
target_compile_options(frame_bandwidth_check PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(frame_bandwidth_check PRIVATE -fsanitize=address,undefined)
target_link_libraries(frame_bandwidth_check PRIVATE Threads::Threads)

add_subdirectory(cells)

option(SIXDB_FRAME_CODEC_CONTROLS "Build pinned native Frame codec controls" OFF)
if(SIXDB_FRAME_CODEC_CONTROLS)
  add_subdirectory(codecs)
endif()

option(SIXDB_FRAME_SYSTEM_CONTROLS "Build native Frame query-engine controls" OFF)
if(SIXDB_FRAME_SYSTEM_CONTROLS)
  add_subdirectory(systems)
endif()

option(SIXDB_FRAME_SELECTION_CONTROLS "Build pinned native Frame set and sort controls" OFF)
if(SIXDB_FRAME_SELECTION_CONTROLS)
  add_subdirectory(selection-controls)
endif()
