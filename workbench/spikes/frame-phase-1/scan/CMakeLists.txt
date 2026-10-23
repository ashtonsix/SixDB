# ctypes uses a private C boundary; ordinary Frame targets remain unaffected.
if(CMAKE_POSITION_INDEPENDENT_CODE)
    add_library(frame_scan_bridge SHARED EXCLUDE_FROM_ALL bridge.cpp)
    sixdb_target(frame_scan_bridge)
    target_link_libraries(frame_scan_bridge PRIVATE frame_encoding_material)
    target_link_options(frame_scan_bridge PRIVATE -Wl,--no-undefined)
endif()
