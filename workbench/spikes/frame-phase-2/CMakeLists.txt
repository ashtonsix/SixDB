add_subdirectory(substrate)
add_subdirectory(bodies)  # front C
add_subdirectory(seriespack)
add_subdirectory(runs)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/instruments/CMakeLists.txt)
    add_subdirectory(instruments)  # front A, Ikea LEAD
endif()
