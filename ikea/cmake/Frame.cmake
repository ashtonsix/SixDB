add_library(ikea_frame STATIC
  src/frame/description.cpp src/frame/manifest.cpp src/frame/group_image.cpp
  src/frame/image.cpp src/frame/reader.cpp src/frame/stage.cpp
  src/frame/stages/pack.cpp src/frame/stages/bound.cpp src/frame/stages/intervals.cpp
  src/frame/build.cpp src/frame/execution.cpp)
add_library(ikea::frame ALIAS ikea_frame)
sixdb_target(ikea_frame)
target_include_directories(ikea_frame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_frame PUBLIC cxx_std_23)
target_link_libraries(ikea_frame PUBLIC ikea::seriespack ikea::intervals)

foreach(check description manifest group_image reader stages span demand)
  add_executable(ikea_frame_${check}_check EXCLUDE_FROM_ALL test/frame/${check}.cpp)
  sixdb_target(ikea_frame_${check}_check)
  target_link_libraries(ikea_frame_${check}_check PRIVATE ikea::frame)
  add_test(NAME ikea_frame_${check}_check COMMAND $<TARGET_FILE:ikea_frame_${check}_check>)
  add_dependencies(ikea_validate ikea_frame_${check}_check)
endforeach()

add_executable(ikea_example_frame_ordinary EXCLUDE_FROM_ALL examples/frame/ordinary.cpp)
sixdb_target(ikea_example_frame_ordinary)
target_link_libraries(ikea_example_frame_ordinary PRIVATE ikea::frame)
add_test(NAME ikea_example_frame_ordinary COMMAND $<TARGET_FILE:ikea_example_frame_ordinary>)
add_dependencies(ikea_examples ikea_example_frame_ordinary)

# Integration material is an example owner, never a dependency of ikea::frame.
add_executable(ikea_example_frame_owner EXCLUDE_FROM_ALL
  examples/frame/owner/material.cpp examples/frame/owner/history.cpp)
sixdb_target(ikea_example_frame_owner)
target_link_libraries(ikea_example_frame_owner PRIVATE ikea::frame orbital::orbital)
add_test(NAME ikea_example_frame_owner COMMAND $<TARGET_FILE:ikea_example_frame_owner>)
add_dependencies(ikea_examples ikea_example_frame_owner)
