add_subdirectory(substrate)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/instruments/CMakeLists.txt)
    add_subdirectory(instruments)  # front A, Ikea LEAD
endif()
