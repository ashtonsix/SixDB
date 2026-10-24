# Matched corpus operations after the shared stages and controls are available.
add_executable(frame3_frame_corpus EXCLUDE_FROM_ALL frame.cpp)
sixdb_target(frame3_frame_corpus)
target_link_libraries(frame3_frame_corpus PRIVATE frame3_widths frame_cells_controls)
add_test(NAME frame3_frame_corpus_check COMMAND frame3_frame_corpus --check-only)
