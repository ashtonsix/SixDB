# The captured study supplies exact Frame source/header exports. Common native
# leaves are shared so these arms isolate Frame lowering and stage laws.
foreach(arm base qualified)
  set(control_root "${CMAKE_CURRENT_SOURCE_DIR}/landing/control-${arm}/ikea")
  file(GLOB control_sources CONFIGURE_DEPENDS
    "${control_root}/src/frame/*.cpp" "${control_root}/src/frame/stages/*.cpp")
  add_library(frame_seam_${arm} STATIC EXCLUDE_FROM_ALL ${control_sources})
  sixdb_target(frame_seam_${arm})
  target_include_directories(frame_seam_${arm} BEFORE PUBLIC "${control_root}/include")
  target_link_libraries(frame_seam_${arm} PUBLIC ikea::seriespack ikea::intervals)
  add_executable(frame_seam_${arm}_span EXCLUDE_FROM_ALL span_compare.cpp)
  sixdb_target(frame_seam_${arm}_span)
  if(arm STREQUAL "base")
    target_compile_definitions(frame_seam_${arm}_span PRIVATE FRAME_SPAN_INTERFACE=0)
  else()
    target_compile_definitions(frame_seam_${arm}_span PRIVATE FRAME_SPAN_INTERFACE=1)
  endif()
  target_link_libraries(frame_seam_${arm}_span PRIVATE frame_seam_${arm})
  add_executable(frame_seam_${arm}_measure EXCLUDE_FROM_ALL seam.cpp)
  sixdb_target(frame_seam_${arm}_measure)
  target_link_libraries(frame_seam_${arm}_measure PRIVATE frame_seam_${arm})
  add_executable(frame_seam_${arm}_landing EXCLUDE_FROM_ALL
    "${PROJECT_SOURCE_DIR}/workbench/spikes/frame-phase-3/landing/measure.cpp")
  sixdb_target(frame_seam_${arm}_landing)
  target_link_libraries(frame_seam_${arm}_landing PRIVATE frame_seam_${arm} frame3_widths frame_cells_controls)
endforeach()

# Header-only leaf bodies with exactly one expression.h varied. Admission and
# overlap compile with each arm; no erased read table can replace these bodies.
foreach(arm old current)
  add_executable(frame_seam_leaf_${arm} EXCLUDE_FROM_ALL
    leaf/exact_halves.cpp "${PROJECT_SOURCE_DIR}/ikea/src/seriespack/admission.cpp"
    "${PROJECT_SOURCE_DIR}/ikea/src/overlap.cpp")
  sixdb_target(frame_seam_leaf_${arm})
  target_include_directories(frame_seam_leaf_${arm} PRIVATE "${PROJECT_SOURCE_DIR}/ikea/include")
  if(arm STREQUAL "old")
    target_include_directories(frame_seam_leaf_${arm} BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/leaf/old-include")
  endif()
endforeach()

add_executable(frame_seam_mask_kernels EXCLUDE_FROM_ALL mask/kernels.cpp mask/bench.cpp)
sixdb_target(frame_seam_mask_kernels)
