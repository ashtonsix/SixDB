include("${PROJECT_SOURCE_DIR}/workbench/peers/frame-codecs.cmake")
foreach(peer IN ITEMS turbopfor fastlanes alp)
  add_executable(frame_${peer}_check ${peer}_check.cpp)
  sixdb_target(frame_${peer}_check)
endforeach()
target_link_libraries(frame_turbopfor_check PRIVATE workbench::turbopfor)
target_link_libraries(frame_fastlanes_check PRIVATE workbench::fastlanes_ffor)
target_link_libraries(frame_alp_check PRIVATE workbench::alp)
target_compile_definitions(frame_turbopfor_check PRIVATE "WORKBENCH_PEER_REVISION=\"${WORKBENCH_TURBOPFOR_REVISION}\"")
target_compile_definitions(frame_fastlanes_check PRIVATE "WORKBENCH_PEER_REVISION=\"${WORKBENCH_FASTLANES_REVISION}\"")
target_compile_definitions(frame_alp_check PRIVATE "WORKBENCH_PEER_REVISION=\"${WORKBENCH_ALP_REVISION}\"")

add_executable(frame_turbopfor_block_check turbopfor_block_check.cpp)
sixdb_target(frame_turbopfor_block_check)
target_link_libraries(frame_turbopfor_block_check PRIVATE workbench::turbopfor)
target_compile_definitions(frame_turbopfor_block_check PRIVATE
  "WORKBENCH_PEER_REVISION=\"${WORKBENCH_TURBOPFOR_REVISION}\"")

# This instruments the inline caller/preparation/accessor, not the upstream
# make-built static library. Ordinary placement retains a known negative.
add_executable(frame_turbopfor_block_wrapper_sanitize EXCLUDE_FROM_ALL turbopfor_block_check.cpp)
sixdb_target(frame_turbopfor_block_wrapper_sanitize)
target_link_libraries(frame_turbopfor_block_wrapper_sanitize PRIVATE workbench::turbopfor)
target_compile_definitions(frame_turbopfor_block_wrapper_sanitize PRIVATE
  "WORKBENCH_PEER_REVISION=\"${WORKBENCH_TURBOPFOR_REVISION}\"")
target_compile_options(frame_turbopfor_block_wrapper_sanitize PRIVATE
  -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(frame_turbopfor_block_wrapper_sanitize PRIVATE -fsanitize=address,undefined)
