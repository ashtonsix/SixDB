add_library(ikea_seriespack STATIC src/seriespack/seriespack.cpp)
add_library(ikea::seriespack ALIAS ikea_seriespack)
sixdb_target(ikea_seriespack)
target_include_directories(ikea_seriespack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_seriespack PUBLIC cxx_std_23)

add_executable(ikea_hello examples/hello.cpp)
sixdb_target(ikea_hello)
target_link_libraries(ikea_hello PRIVATE ikea::seriespack)
