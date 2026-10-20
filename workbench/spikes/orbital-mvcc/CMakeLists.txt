# Current production consequence/integration checks. Historical benchmark builds
# and rejected prototype matrices are recoverable from the retirement source cut.
add_subdirectory(consumer)
add_subdirectory(metadata)

add_executable(orbital_pack_growth_check ../../benchmarks/orbital/growth.cpp)
sixdb_target(orbital_pack_growth_check)
target_compile_options(orbital_pack_growth_check PRIVATE -UNDEBUG)
target_link_libraries(orbital_pack_growth_check PRIVATE loom::loom ikea::seriespack ikea::tuplepack)
add_test(NAME orbital_pack_growth_check COMMAND orbital_pack_growth_check)
set_tests_properties(orbital_pack_growth_check PROPERTIES LABELS orbital-mvcc-metadata TIMEOUT 60)
add_dependencies(orbital_mvcc_metadata_validate orbital_pack_growth_check)

add_custom_target(orbital_mvcc_validate DEPENDS orbital_mvcc_metadata_validate)
