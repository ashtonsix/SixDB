add_subdirectory(substrate)
add_subdirectory(bodies)  # front C
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/instruments/CMakeLists.txt)
    add_subdirectory(instruments)  # front A, Ikea LEAD
endif()
