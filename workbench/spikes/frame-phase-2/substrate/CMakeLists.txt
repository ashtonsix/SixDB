# Front B probe: kind-batched execution over phase 1 images. Needs
# -DSIXDB_SPIKES='frame-phase-1;frame-phase-2' (phase 1 supplies the encoder).
if(NOT TARGET frame_encoding_material)
    message(FATAL_ERROR "frame-phase-2/substrate needs -DSIXDB_SPIKES=frame-phase-1;frame-phase-2 in that order")
endif()
add_executable(frame_substrate_measure measure.cpp plan.cpp)
sixdb_target(frame_substrate_measure)
target_link_libraries(frame_substrate_measure PRIVATE frame_encoding_material)
add_executable(frame_substrate_select select.cpp)
sixdb_target(frame_substrate_select)
target_link_libraries(frame_substrate_select PRIVATE frame_encoding_material)
