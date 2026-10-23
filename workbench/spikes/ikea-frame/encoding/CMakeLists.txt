# Private experimental material shared by the driver, Codex's access check and
# Engine's consumers. Codecs compile once here; no public Frame API.
add_library(frame_encoding_material STATIC material.cpp kernels.cpp packing.cpp predicate.cpp)
sixdb_target(frame_encoding_material)
target_include_directories(frame_encoding_material PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frame_encoding_material PUBLIC ikea::seriespack ikea::intervals)

add_executable(frame_encoding_bodies bodies.cpp)
sixdb_target(frame_encoding_bodies)
target_link_libraries(frame_encoding_bodies PRIVATE frame_encoding_material)

# Borrowed-block extents, guard-page grants and oracle results on real columns.
add_executable(frame_encoding_borrowed_check borrowed_check.cpp)
sixdb_target(frame_encoding_borrowed_check)
target_link_libraries(frame_encoding_borrowed_check PRIVATE frame_encoding_material)

# Codex's protected-payload correctness fixture.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/masked_access_check.cpp)
    add_executable(frame_encoding_masked_access_check masked_access_check.cpp)
    sixdb_target(frame_encoding_masked_access_check)
    target_link_libraries(frame_encoding_masked_access_check PRIVATE frame_encoding_material)
endif()

# NULL fill: boundary pins and real-column laws (null_check), then bits/row
# and row-selected timings under synthetic NULLs (nulls).
add_executable(frame_encoding_null_check null_check.cpp)
sixdb_target(frame_encoding_null_check)
target_link_libraries(frame_encoding_null_check PRIVATE frame_encoding_material)
add_executable(frame_encoding_nulls nulls.cpp)
sixdb_target(frame_encoding_nulls)
target_link_libraries(frame_encoding_nulls PRIVATE frame_encoding_material)

# Fused multi-aggregate programs against separate owner calls and native
# arrays (fused.h; correctness is in borrowed_check).
add_executable(frame_encoding_fused fused.cpp)
sixdb_target(frame_encoding_fused)
target_link_libraries(frame_encoding_fused PRIVATE frame_encoding_material)

# Predicate masks (predicate.h): complete masks against the input oracle,
# coarse-skip guards and end-to-end filter(A) -> SUM/COUNT(B) (predicate_check),
# then emit timings against decode-and-compare (predicates).
add_executable(frame_encoding_predicate_check predicate_check.cpp)
sixdb_target(frame_encoding_predicate_check)
target_link_libraries(frame_encoding_predicate_check PRIVATE frame_encoding_material)
add_executable(frame_encoding_predicates predicates.cpp)
sixdb_target(frame_encoding_predicates)
target_link_libraries(frame_encoding_predicates PRIVATE frame_encoding_material)
