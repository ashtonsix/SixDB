# Dependency pin only; none of the preceding spike's map implementation is used.
include(reference/baseline.cmake)
add_executable(hashtable_design_benchmark_oracle_check benchmark_oracle_check.cpp)
sixdb_target(hashtable_design_benchmark_oracle_check)
target_link_libraries(hashtable_design_benchmark_oracle_check PRIVATE orbital::orbital)
add_test(NAME hashtable_design_benchmark_oracle_check COMMAND hashtable_design_benchmark_oracle_check)
add_executable(hashtable_design_compare compare.cpp)
sixdb_target(hashtable_design_compare)
target_link_libraries(hashtable_design_compare PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_compare PRIVATE NDEBUG)
target_compile_options(hashtable_design_compare PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_design_check COMMAND hashtable_design_compare check)
add_executable(hashtable_design_ring ring.cpp)
sixdb_target(hashtable_design_ring)
target_link_libraries(hashtable_design_ring PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital loom::loom)
target_compile_definitions(hashtable_design_ring PRIVATE NDEBUG)
target_compile_options(hashtable_design_ring PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_design_ring_check COMMAND hashtable_design_ring check)
add_executable(hashtable_design_aggregate aggregate.cpp)
sixdb_target(hashtable_design_aggregate)
target_link_libraries(hashtable_design_aggregate PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_aggregate PRIVATE NDEBUG)
target_compile_options(hashtable_design_aggregate PRIVATE -fno-vectorize -fno-slp-vectorize)
# Apply the same inlining budget to every accumulation arm. Public try_emplace
# forwards through private search layers; caller attributes alone do not force
# their recursive fusion in Clang 21. The final binary still must pass its guard.
target_compile_options(hashtable_design_aggregate PRIVATE -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_aggregate_check COMMAND hashtable_design_aggregate check)
add_executable(hashtable_design_workers workers.cpp)
sixdb_target(hashtable_design_workers)
target_link_libraries(hashtable_design_workers PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_workers PRIVATE NDEBUG)
target_compile_options(hashtable_design_workers PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_workers_check COMMAND hashtable_design_workers check)
add_executable(hashtable_design_mutation mutation.cpp)
sixdb_target(hashtable_design_mutation)
target_link_libraries(hashtable_design_mutation PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_mutation PRIVATE NDEBUG)
target_compile_options(hashtable_design_mutation PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_mutation_check COMMAND hashtable_design_mutation check)
add_executable(hashtable_design_transition transition.cpp)
sixdb_target(hashtable_design_transition)
target_link_libraries(hashtable_design_transition PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_transition PRIVATE NDEBUG)
target_compile_options(hashtable_design_transition PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_transition_check COMMAND hashtable_design_transition check)
add_executable(hashtable_design_set set.cpp)
sixdb_target(hashtable_design_set)
target_link_libraries(hashtable_design_set PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_set PRIVATE NDEBUG)
target_compile_options(hashtable_design_set PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_set_check COMMAND hashtable_design_set check)
foreach(check IN ITEMS solver hash_packet domain_advisory)
  add_executable(hashtable_design_${check}_check ${check}_check.cpp)
  sixdb_target(hashtable_design_${check}_check)
  target_link_libraries(hashtable_design_${check}_check PRIVATE orbital::orbital)
  target_compile_options(hashtable_design_${check}_check PRIVATE -fno-vectorize -fno-slp-vectorize)
  add_test(NAME hashtable_design_${check}_check COMMAND hashtable_design_${check}_check)
endforeach()

# Complete-operation hash lowering controls; geometry and selectors stay fixed.
foreach(variant IN ITEMS ordinary split paired and)
  if(variant STREQUAL "ordinary")
    set(mask_mode 0)
  elseif(variant STREQUAL "split")
    set(mask_mode 1)
  elseif(variant STREQUAL "paired")
    set(mask_mode 2)
  else()
    set(mask_mode 3)
  endif()
  set(check hashtable_design_hash_mask_${variant}_check)
  add_executable(${check} hash_mask_check.cpp)
  sixdb_target(${check})
  target_link_libraries(${check} PRIVATE orbital::orbital)
  target_compile_definitions(${check} PRIVATE HT_HASH_MASK=${mask_mode})
  target_compile_options(${check} PRIVATE -fno-vectorize -fno-slp-vectorize)
  add_test(NAME ${check} COMMAND ${check})
endforeach()

add_executable(hashtable_design_small small.cpp)
sixdb_target(hashtable_design_small)
target_link_libraries(hashtable_design_small PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_small PRIVATE NDEBUG)
target_compile_options(hashtable_design_small PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_small_check COMMAND hashtable_design_small check)
add_executable(hashtable_design_small_scan64 small.cpp)
sixdb_target(hashtable_design_small_scan64)
target_link_libraries(hashtable_design_small_scan64 PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_small_scan64 PRIVATE NDEBUG HT_LIST_SCAN64=1)
target_compile_options(hashtable_design_small_scan64 PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_small_scan64_check COMMAND hashtable_design_small_scan64 check)
add_executable(hashtable_design_small_publication small_publication.cpp)
sixdb_target(hashtable_design_small_publication)
target_link_libraries(hashtable_design_small_publication PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_include_directories(hashtable_design_small_publication PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_small_publication PRIVATE NDEBUG)
target_compile_options(hashtable_design_small_publication PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_small_publication_check COMMAND hashtable_design_small_publication check)
add_executable(hashtable_design_links_reservation_check links_reservation_check.cpp)
sixdb_target(hashtable_design_links_reservation_check)
target_link_libraries(hashtable_design_links_reservation_check PRIVATE
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_options(hashtable_design_links_reservation_check PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_design_links_reservation_check COMMAND hashtable_design_links_reservation_check)
add_executable(hashtable_design_rich_composition rich_composition.cpp)
sixdb_target(hashtable_design_rich_composition)
target_link_libraries(hashtable_design_rich_composition PRIVATE
  ikea::seriespack ikea::tuplepack orbital::orbital)
target_compile_options(hashtable_design_rich_composition PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_design_rich_composition_check COMMAND hashtable_design_rich_composition check)
add_executable(hashtable_design_list_transition list_transition.cpp)
sixdb_target(hashtable_design_list_transition)
target_link_libraries(hashtable_design_list_transition PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_include_directories(hashtable_design_list_transition PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_list_transition PRIVATE NDEBUG)
target_compile_options(hashtable_design_list_transition PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_list_transition_check COMMAND hashtable_design_list_transition check)
add_executable(hashtable_design_small_batch small_batch.cpp)
sixdb_target(hashtable_design_small_batch)
target_link_libraries(hashtable_design_small_batch PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_small_batch PRIVATE NDEBUG)
target_compile_options(hashtable_design_small_batch PRIVATE -fno-vectorize -fno-slp-vectorize
  -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_small_batch_check COMMAND hashtable_design_small_batch check)

# One stored format, two exclusive-owner capacity policies. These are spike
# controls, not a public option; complete small-list histories compare both.
foreach(consumer IN ITEMS small small_batch small_publication list_transition)
  set(target hashtable_design_${consumer}_grow)
  add_executable(${target} ${consumer}.cpp)
  sixdb_target(${target})
  target_link_libraries(${target} PRIVATE hashtable_abseil_baseline
    ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${target} PRIVATE NDEBUG HT_LIST_GROW_PRIVATE=1)
  target_compile_options(${target} PRIVATE -fno-vectorize -fno-slp-vectorize
    -mllvm -inline-threshold=1000)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()
add_executable(hashtable_design_list_growth_check list_growth_check.cpp)
sixdb_target(hashtable_design_list_growth_check)
target_link_libraries(hashtable_design_list_growth_check PRIVATE
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_list_growth_check PRIVATE HT_LIST_GROW_PRIVATE=1)
target_compile_options(hashtable_design_list_growth_check PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_design_list_growth_check COMMAND hashtable_design_list_growth_check)
# Split heavy all-domain callers into independent TUs for incremental experiments.
# The same behavior and timed bodies are instantiated; no cross-product is lost.
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(consumer IN ITEMS small small_batch)
    foreach(grow IN ITEMS 0 1)
      set(target hashtable_design_${consumer}_${key}_grow${grow})
      add_executable(${target} ${consumer}.cpp)
      sixdb_target(${target})
      target_link_libraries(${target} PRIVATE hashtable_abseil_baseline
        ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
      target_compile_definitions(${target} PRIVATE NDEBUG HT_SMALL_KEY=${key} HT_LIST_GROW_PRIVATE=${grow})
      target_compile_options(${target} PRIVATE -fno-vectorize -fno-slp-vectorize -mllvm -inline-threshold=1000)
      add_test(NAME ${target} COMMAND ${target} check)
      if(consumer STREQUAL "small_batch")
        set(control ${target}_full)
        add_executable(${control} ${consumer}.cpp)
        sixdb_target(${control})
        target_link_libraries(${control} PRIVATE hashtable_abseil_baseline
          ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
        target_compile_definitions(${control} PRIVATE NDEBUG HT_SMALL_KEY=${key}
          HT_LIST_GROW_PRIVATE=${grow} HT_SMALL_COMPACT_OWNER=0)
        target_compile_options(${control} PRIVATE -fno-vectorize -fno-slp-vectorize
          -mllvm -inline-threshold=1000)
        add_test(NAME ${control} COMMAND ${control} check)
      endif()
    endforeach()
  endforeach()
endforeach()
add_executable(hashtable_design_migration migration.cpp)
function(hashtable_migration_settings target)
  sixdb_target(${target})
  target_link_libraries(${target} PRIVATE hashtable_abseil_baseline
    ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
  target_compile_definitions(${target} PRIVATE NDEBUG)
  target_compile_options(${target} PRIVATE -fno-vectorize -fno-slp-vectorize
    -mllvm -inline-threshold=1000)
endfunction()
hashtable_migration_settings(hashtable_design_migration)
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  set(target hashtable_design_prepared_list_${key})
  add_executable(${target} prepared_list.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_PREPARED_KEY=${key}
    HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()
# Attribute synchronous promotion's old duplicate search separately from the
# bounded-preparation protocol. Same complete lifetime and baseline bodies.
add_executable(hashtable_design_prepared_list_40_checked prepared_list.cpp)
hashtable_migration_settings(hashtable_design_prepared_list_40_checked)
target_compile_definitions(hashtable_design_prepared_list_40_checked PRIVATE
  HT_PREPARED_KEY=40 HT_HASH_MASK=2 HT_RECIPE_SCAN32=1 HT_PROMOTION_UNIQUE=0)
add_test(NAME hashtable_design_prepared_list_40_checked COMMAND hashtable_design_prepared_list_40_checked check)
# A caller-supplied opaque map combiner for worker reductions, with ordinary
# Abseil and the double-search control timed from the first implementation.
foreach(case IN ITEMS 8_1 8_64 16_2 24_3 32_7 40_16 48_8 56_32 64_1 64_64)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  set(target hashtable_design_combine_${case})
  add_executable(${target} combine_bytes.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_COMBINE_KEY=${key} HT_COMBINE_WIDTH=${bytes}
    HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
  add_test(NAME ${target} COMMAND ${target} check)
  set(variant ${target}_scalar_packet)
  add_executable(${variant} combine_bytes.cpp)
  hashtable_migration_settings(${variant})
  target_compile_definitions(${variant} PRIVATE HT_COMBINE_KEY=${key} HT_COMBINE_WIDTH=${bytes}
    HT_HASH_MASK=2 HT_RECIPE_SCAN32=1 HT_SMALL_PACKET_SCALAR=1 HT_HIERARCHY_PACKET_HASH=2)
  add_test(NAME ${variant} COMMAND ${variant} check)
  set(variant ${target}_lookup)
  add_executable(${variant} combine_bytes.cpp)
  hashtable_migration_settings(${variant})
  target_compile_definitions(${variant} PRIVATE HT_COMBINE_KEY=${key} HT_COMBINE_WIDTH=${bytes}
    HT_HASH_MASK=2 HT_RECIPE_SCAN32=1 HT_SMALL_PACKET_SCALAR=1 HT_HIERARCHY_PACKET_HASH=2
    HT_SMALL_PACKET_LOOKUP=1)
  add_test(NAME ${variant} COMMAND ${variant} check)
endforeach()
# Keep each key width independently incremental; one giant TU made every small
# driver correction rerun several minutes of optimization across the surface.
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  set(part hashtable_design_migration_${width})
  add_library(${part} OBJECT migration.cpp)
  hashtable_migration_settings(${part})
  target_compile_definitions(${part} PRIVATE HT_MIGRATION_K=${width})
  target_sources(hashtable_design_migration PRIVATE $<TARGET_OBJECTS:${part}>)
endforeach()
add_test(NAME hashtable_design_migration_check COMMAND hashtable_design_migration check)
add_executable(hashtable_design_formats formats.cpp)
hashtable_migration_settings(hashtable_design_formats)
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(kind IN ITEMS 0 1 2)
    set(part hashtable_design_formats_${width}_${kind})
    add_library(${part} OBJECT formats.cpp)
    hashtable_migration_settings(${part})
    target_compile_definitions(${part} PRIVATE HT_FORMAT_K=${width} HT_FORMAT_KIND=${kind})
    target_sources(hashtable_design_formats PRIVATE $<TARGET_OBJECTS:${part}>)
  endforeach()
endforeach()
add_test(NAME hashtable_design_formats_check COMMAND hashtable_design_formats check)
add_executable(hashtable_design_access formats.cpp)
hashtable_migration_settings(hashtable_design_access)
target_compile_definitions(hashtable_design_access PRIVATE HT_FORMAT_ACCESS=1)
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(kind IN ITEMS 0 1 2)
    set(part hashtable_design_access_${width}_${kind})
    add_library(${part} OBJECT formats.cpp)
    hashtable_migration_settings(${part})
    target_compile_definitions(${part} PRIVATE HT_FORMAT_ACCESS=1 HT_FORMAT_K=${width} HT_FORMAT_KIND=${kind})
    target_sources(hashtable_design_access PRIVATE $<TARGET_OBJECTS:${part}>)
  endforeach()
endforeach()
add_test(NAME hashtable_design_access_check COMMAND hashtable_design_access check)
add_executable(hashtable_design_append formats.cpp)
hashtable_migration_settings(hashtable_design_append)
target_compile_definitions(hashtable_design_append PRIVATE HT_FORMAT_APPEND=1)
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(kind IN ITEMS 0 1 2)
    set(part hashtable_design_append_${width}_${kind})
    add_library(${part} OBJECT formats.cpp)
    hashtable_migration_settings(${part})
    target_compile_definitions(${part} PRIVATE HT_FORMAT_APPEND=1 HT_FORMAT_K=${width} HT_FORMAT_KIND=${kind})
    target_sources(hashtable_design_append PRIVATE $<TARGET_OBJECTS:${part}>)
  endforeach()
endforeach()
add_test(NAME hashtable_design_append_check COMMAND hashtable_design_append check)
add_executable(hashtable_design_reduction formats.cpp)
hashtable_migration_settings(hashtable_design_reduction)
target_compile_definitions(hashtable_design_reduction PRIVATE HT_FORMAT_ACCESS=1 HT_FORMAT_LOCAL_REDUCTION=1)
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(kind IN ITEMS 0 1 2)
    set(part hashtable_design_reduction_${width}_${kind})
    add_library(${part} OBJECT formats.cpp)
    hashtable_migration_settings(${part})
    target_compile_definitions(${part} PRIVATE HT_FORMAT_ACCESS=1 HT_FORMAT_LOCAL_REDUCTION=1 HT_FORMAT_K=${width} HT_FORMAT_KIND=${kind})
    target_sources(hashtable_design_reduction PRIVATE $<TARGET_OBJECTS:${part}>)
  endforeach()
endforeach()
add_test(NAME hashtable_design_reduction_check COMMAND hashtable_design_reduction check)
add_executable(hashtable_design_bag_formats formats.cpp)
hashtable_migration_settings(hashtable_design_bag_formats)
target_compile_definitions(hashtable_design_bag_formats PRIVATE HT_FORMAT_BAGS=1 HT_FORMAT_LOCAL_REDUCTION=1)
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(kind IN ITEMS 0 1 2)
    set(part hashtable_design_bag_formats_${width}_${kind})
    add_library(${part} OBJECT formats.cpp)
    hashtable_migration_settings(${part})
    target_compile_definitions(${part} PRIVATE HT_FORMAT_BAGS=1 HT_FORMAT_LOCAL_REDUCTION=1 HT_FORMAT_K=${width} HT_FORMAT_KIND=${kind})
    target_sources(hashtable_design_bag_formats PRIVATE $<TARGET_OBJECTS:${part}>)
  endforeach()
endforeach()
add_test(NAME hashtable_design_bag_formats_check COMMAND hashtable_design_bag_formats check)
add_executable(hashtable_design_buffer_bags_check buffer_bags_check.cpp)
hashtable_migration_settings(hashtable_design_buffer_bags_check)
add_test(NAME hashtable_design_buffer_bags_check COMMAND hashtable_design_buffer_bags_check)
add_executable(hashtable_design_buffer_publication small_publication.cpp)
hashtable_migration_settings(hashtable_design_buffer_publication)
target_include_directories(hashtable_design_buffer_publication PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_buffer_publication PRIVATE HT_PUBLICATION_BUFFER_BAGS=1)
add_test(NAME hashtable_design_buffer_publication_check COMMAND hashtable_design_buffer_publication check)
add_executable(hashtable_design_recipe_view_check recipe_view_check.cpp)
hashtable_migration_settings(hashtable_design_recipe_view_check)
add_test(NAME hashtable_design_recipe_view_check COMMAND hashtable_design_recipe_view_check)
add_executable(hashtable_design_recipe_scan32_check recipe_view_check.cpp)
hashtable_migration_settings(hashtable_design_recipe_scan32_check)
target_compile_definitions(hashtable_design_recipe_scan32_check PRIVATE HT_RECIPE_SCAN32=1)
add_test(NAME hashtable_design_recipe_scan32_check COMMAND hashtable_design_recipe_scan32_check)
option(SIXDB_HASHTABLE_CONCURRENT_BASELINES "Build pinned Boost/GTL concurrent baseline controls" OFF)
if(SIXDB_HASHTABLE_CONCURRENT_BASELINES)
  include(concurrent_baselines.cmake)
endif()
option(SIXDB_HASHTABLE_CONCURRENT_WORKERS "Build the two-worker shared-table baseline bridge" OFF)
if(SIXDB_HASHTABLE_CONCURRENT_WORKERS)
  include(concurrent_workers.cmake)
endif()

# One/two-inline short-bag representation control, with complete-operation baselines.
add_executable(hashtable_design_inline_formats formats.cpp)
hashtable_migration_settings(hashtable_design_inline_formats)
target_compile_definitions(hashtable_design_inline_formats PRIVATE HT_FORMAT_INLINE=1 HT_FORMAT_LOCAL_REDUCTION=1)
foreach(width IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(kind IN ITEMS 0 1 2)
    set(part hashtable_design_inline_formats_${width}_${kind})
    add_library(${part} OBJECT formats.cpp)
    hashtable_migration_settings(${part})
    target_compile_definitions(${part} PRIVATE HT_FORMAT_INLINE=1 HT_FORMAT_LOCAL_REDUCTION=1 HT_FORMAT_K=${width} HT_FORMAT_KIND=${kind})
    target_sources(hashtable_design_inline_formats PRIVATE $<TARGET_OBJECTS:${part}>)
  endforeach()
endforeach()
add_test(NAME hashtable_design_inline_formats_check COMMAND hashtable_design_inline_formats check)
add_executable(hashtable_design_inline_publication small_publication.cpp)
hashtable_migration_settings(hashtable_design_inline_publication)
target_include_directories(hashtable_design_inline_publication PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_inline_publication PRIVATE HT_PUBLICATION_INLINE=2)
add_test(NAME hashtable_design_inline_publication_check COMMAND hashtable_design_inline_publication check)
add_executable(hashtable_design_inline_bag_check inline_bag_check.cpp)
hashtable_migration_settings(hashtable_design_inline_bag_check)
add_test(NAME hashtable_design_inline_bag_check COMMAND hashtable_design_inline_bag_check)

# Complete byte-width operations: separate TUs keep each width incremental.
foreach(bytes IN ITEMS 1 2 3 4 7 8 9 16 17 32 64)
  set(target hashtable_design_value_${bytes}_check)
  add_executable(${target} value_width_check.cpp)
  hashtable_migration_settings(${target})
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=${bytes})
  add_test(NAME ${target} COMMAND ${target})
endforeach()

foreach(bytes IN ITEMS 2 8 16)
  set(target hashtable_design_value_${bytes}_bench)
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=${bytes})
  set(policy hashtable_design_bag_policy_${bytes}_bench)
  add_executable(${policy} value_width_bench.cpp)
  hashtable_migration_settings(${policy})
  target_compile_definitions(${policy} PRIVATE HT_VALUE_BYTES=${bytes} HT_BAG_POLICIES=1)
  set(check hashtable_design_bag_policy_${bytes}_check)
  add_executable(${check} bag_policy_check.cpp)
  hashtable_migration_settings(${check})
  target_compile_definitions(${check} PRIVATE HT_BAG_POLICY_VALUE_BYTES=${bytes})
  add_test(NAME ${check} COMMAND ${check})

  set(events hashtable_design_bag_policy_${bytes}_events)
  add_executable(${events} bag_policy_events.cpp)
  hashtable_migration_settings(${events})
  target_compile_definitions(${events} PRIVATE HT_BAG_POLICY_VALUE_BYTES=${bytes})
endforeach()

add_executable(hashtable_design_mutation_patch_check mutation_patch_check.cpp)
hashtable_migration_settings(hashtable_design_mutation_patch_check)
target_include_directories(hashtable_design_mutation_patch_check PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
add_test(NAME hashtable_design_mutation_patch_check COMMAND hashtable_design_mutation_patch_check)

# Cardinality stays fixed while each control changes physical hierarchy geometry.
# K8's domain fits the separate whole-table-list regime.
foreach(key IN ITEMS 16 24 32 40 48 56 64)
  foreach(bytes IN ITEMS 0 2 8 16)
    set(target hashtable_design_geometry_${key}_${bytes})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    if(bytes EQUAL 0)
      set(kind 0)
    else()
      set(kind 1)
    endif()
    target_compile_definitions(${target} PRIVATE HT_GEOMETRY_POLICIES=1
      HT_GEOMETRY_KIND=${kind} HT_VALUE_BYTES=${bytes} HT_VALUE_KEY=${key})
  endforeach()
endforeach()

# Matched execution controls over the unchanged geometry panel. This finite
# first comparison exercises narrow/wider-key maps with dense selector planes.
foreach(key IN ITEMS 16 40)
  foreach(variant IN ITEMS composed16 direct16 composed32 direct32)
    set(target hashtable_design_selector_${key}_2_${variant})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    if(variant MATCHES "^direct")
      set(direct 1)
    else()
      set(direct 0)
    endif()
    if(variant MATCHES "32$")
      set(scan32 1)
    else()
      set(scan32 0)
    endif()
    target_compile_definitions(${target} PRIVATE HT_GEOMETRY_POLICIES=1
      HT_GEOMETRY_KIND=1 HT_VALUE_BYTES=2 HT_VALUE_KEY=${key}
      HT_VALUE_DIRECT=${direct} HT_RECIPE_SCAN32=${scan32})
  endforeach()
endforeach()

# Ordinary 16-byte selectors isolate the hash lowering intervention from scan32.
foreach(variant IN ITEMS ordinary split paired and)
  if(variant STREQUAL "ordinary")
    set(mask_mode 0)
  elseif(variant STREQUAL "split")
    set(mask_mode 1)
  elseif(variant STREQUAL "paired")
    set(mask_mode 2)
  else()
    set(mask_mode 3)
  endif()
  foreach(bytes IN ITEMS 0 2)
    set(target hashtable_design_mask_40_${bytes}_${variant})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    if(bytes EQUAL 0)
      set(kind 0)
    else()
      set(kind 1)
    endif()
    target_compile_definitions(${target} PRIVATE HT_GEOMETRY_POLICIES=1
      HT_GEOMETRY_KIND=${kind} HT_VALUE_BYTES=${bytes} HT_VALUE_KEY=40
      HT_VALUE_DIRECT=0 HT_RECIPE_SCAN32=0 HT_HASH_MASK=${mask_mode})
  endforeach()
endforeach()

# Inactive byte fingerprints: fixed geometry and full complete-operation costs.
add_executable(hashtable_design_row_consume_check inactive_check.cpp)
hashtable_migration_settings(hashtable_design_row_consume_check)
target_compile_definitions(hashtable_design_row_consume_check PRIVATE HT_BUCKET_SENTINEL=1 HT_RECIPE_SCAN32=1 HT_RECORD_CONSUME=1)
add_test(NAME hashtable_design_row_consume_check COMMAND hashtable_design_row_consume_check)
add_executable(hashtable_design_inactive_check inactive_check.cpp)
hashtable_migration_settings(hashtable_design_inactive_check)
target_compile_definitions(hashtable_design_inactive_check PRIVATE HT_BUCKET_SENTINEL=1 HT_RECIPE_SCAN32=1)
add_test(NAME hashtable_design_inactive_check COMMAND hashtable_design_inactive_check)
add_executable(hashtable_design_inactive_values value_width_check.cpp)
hashtable_migration_settings(hashtable_design_inactive_values)
target_include_directories(hashtable_design_inactive_values PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_inactive_values PRIVATE HT_VALUE_BYTES=2 HT_BUCKET_SENTINEL=1 HT_RECIPE_SCAN32=1)
add_test(NAME hashtable_design_inactive_values COMMAND hashtable_design_inactive_values)
foreach(case IN ITEMS 16_2_1 40_0_0 40_2_1 40_8_2 64_16_1)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  foreach(encoding IN ITEMS count inactive)
    set(target hashtable_design_occupancy_${case}_${encoding})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    if(encoding STREQUAL "inactive")
      set(sentinel 1)
    else()
      set(sentinel 0)
    endif()
    target_compile_definitions(${target} PRIVATE HT_GEOMETRY_POLICIES=1 HT_OCCUPANCY_STUDY=1
      HT_GEOMETRY_KIND=${kind} HT_VALUE_BYTES=${bytes} HT_VALUE_KEY=${key}
      HT_BUCKET_SENTINEL=${sentinel} HT_RECIPE_SCAN32=1 HT_HASH_MASK=2)
  endforeach()
endforeach()

# Keep layout, immediate consumer and independent direct loads as separate axes.
foreach(case IN ITEMS 16_2_1 40_2_1 40_8_2 64_16_1)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  foreach(variant IN ITEMS planes rows consume direct)
    set(target hashtable_design_consumption_${case}_${variant})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    set(rows 1)
    set(consume 1)
    set(direct 0)
    if(variant STREQUAL "planes")
      set(rows 0)
      set(consume 0)
    elseif(variant STREQUAL "rows")
      set(consume 0)
    elseif(variant STREQUAL "direct")
      set(direct 1)
    endif()
    target_compile_definitions(${target} PRIVATE HT_GEOMETRY_POLICIES=1 HT_OCCUPANCY_STUDY=1
      HT_GEOMETRY_KIND=${kind} HT_VALUE_BYTES=${bytes} HT_VALUE_KEY=${key}
      HT_BUCKET_SENTINEL=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_RECORD_CONSUME=${consume}
      HT_VALUE_ROWS=${rows} HT_VALUE_DIRECT=${direct})
  endforeach()
endforeach()

# Selector arithmetic changes fingerprint contents, never bucket placement.
# Use the same complete history and paired ordinary/exact-key baselines from
# the first build. The new family is default-off throughout the spike.
add_executable(hashtable_design_selector_quality selector_quality.cpp)
sixdb_target(hashtable_design_selector_quality)
target_link_libraries(hashtable_design_selector_quality PRIVATE orbital::orbital)
target_compile_options(hashtable_design_selector_quality PRIVATE -fno-vectorize -fno-slp-vectorize)
foreach(variant IN ITEMS fold multiply)
  set(target hashtable_design_selector_witness_${variant})
  add_executable(${target} selector_witness.cpp)
  hashtable_migration_settings(${target})
  set(mode 0)
  if(variant STREQUAL "multiply")
    set(mode 1)
  endif()
  target_compile_definitions(${target} PRIVATE HT_SELECTOR_HASH=${mode} HT_BUCKET_SENTINEL=0 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2)
endforeach()
foreach(check IN ITEMS hash_packet hash_mask)
  set(target hashtable_design_selector_${check}_check)
  add_executable(${target} ${check}_check.cpp)
  sixdb_target(${target})
  target_link_libraries(${target} PRIVATE orbital::orbital)
  target_compile_definitions(${target} PRIVATE HT_SELECTOR_HASH=1 HT_HASH_MASK=2)
  target_compile_options(${target} PRIVATE -fno-vectorize -fno-slp-vectorize)
  add_test(NAME ${target} COMMAND ${target})
endforeach()
add_executable(hashtable_design_selector_values_check value_width_check.cpp)
hashtable_migration_settings(hashtable_design_selector_values_check)
target_include_directories(hashtable_design_selector_values_check PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_selector_values_check PRIVATE HT_SELECTOR_HASH=1 HT_VALUE_BYTES=2)
add_test(NAME hashtable_design_selector_values_check COMMAND hashtable_design_selector_values_check)
foreach(case IN ITEMS 16_2_1 40_0_0 40_2_1 40_8_2 64_16_1)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  foreach(variant IN ITEMS fold multiply)
    set(target hashtable_design_selector_mix_${case}_${variant})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    set(mode 0)
    if(variant STREQUAL "multiply")
      set(mode 1)
    endif()
    target_compile_definitions(${target} PRIVATE HT_GEOMETRY_POLICIES=1 HT_OCCUPANCY_STUDY=1
      HT_GEOMETRY_KIND=${kind} HT_VALUE_BYTES=${bytes} HT_VALUE_KEY=${key}
      HT_SELECTOR_HASH=${mode} HT_BUCKET_SENTINEL=0 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2)
  endforeach()
endforeach()

# Complete hierarchy packets: explicit SIMD hash facts, scalar-equivalent lane
# mutation semantics, opaque values and ordinary/exact Abseil in the first driver.
set(packet_cases)
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  list(APPEND packet_cases ${key}_0_0 ${key}_2_1 ${key}_2_2)
endforeach()
foreach(bytes IN ITEMS 1 3 8 16 64)
  list(APPEND packet_cases 40_${bytes}_1 40_${bytes}_2)
endforeach()
foreach(case IN LISTS packet_cases)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  set(target hashtable_design_hierarchy_packet_${case})
  add_executable(${target} hierarchy_batch.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_PACKET_KEY=${key} HT_PACKET_BYTES=${bytes}
    HT_PACKET_KIND=${kind} HT_RECIPE_SCAN32=1 HT_HASH_MASK=2)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()

# Same lane body and format, with independent hash-preparation policies.
foreach(case IN LISTS packet_cases)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  foreach(mode IN ITEMS 1 2)
    set(target hashtable_design_hierarchy_packet_${case}_hash${mode})
    add_executable(${target} hierarchy_batch.cpp)
    hashtable_migration_settings(${target})
    target_compile_definitions(${target} PRIVATE HT_PACKET_KEY=${key} HT_PACKET_BYTES=${bytes}
      HT_PACKET_KIND=${kind} HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_HIERARCHY_PACKET_HASH=${mode})
    add_test(NAME ${target} COMMAND ${target} check)
  endforeach()
endforeach()

# One shared lane body versus the original complete-operation unrolling.
foreach(kind IN ITEMS 1 2)
  set(target hashtable_design_hierarchy_packet_40_2_${kind}_unrolled)
  add_executable(${target} hierarchy_batch.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_PACKET_KEY=40 HT_PACKET_BYTES=2
    HT_PACKET_KIND=${kind} HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_HIERARCHY_PACKET_UNROLL=1)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()

# Compact SIMD hash facts retain exactly the same permutation and lane semantics.
# Keep this separate from arithmetic changes and operation-body unrolling.
foreach(case IN LISTS packet_cases)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  set(target hashtable_design_hierarchy_packet_${case}_compact)
  add_executable(${target} hierarchy_batch.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_PACKET_KEY=${key} HT_PACKET_BYTES=${bytes}
    HT_PACKET_KIND=${kind} HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_HIERARCHY_PACKET_COMPACT=1)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  set(target hashtable_design_compact_hash_check_${key})
  add_executable(${target} hash_packet_check.cpp)
  sixdb_target(${target})
  target_link_libraries(${target} PRIVATE orbital::orbital)
  target_compile_options(${target} PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_compile_definitions(${target} PRIVATE HT_HASH_CHECK_KEY=${key} HT_CHECK_COMPACT=1)
  add_test(NAME ${target} COMMAND ${target})
endforeach()

# Compact owner-state control: stored hash/count facts, identical packed bytes.
# Separate TUs keep all eight key domains practical to build incrementally.
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  set(target hashtable_design_compact_state_${key})
  add_executable(${target} compact_state.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_STATE_KEY=${key} HT_LIST_GROW_PRIVATE=0)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()

# Same operations/layouts with the fully initialized StreamPack input supplied
# by one immutable zero source. Keep this a caller comparison until measured.
foreach(bytes IN ITEMS 32 64)
  set(target hashtable_design_wide_bag_40_${bytes}_zero)
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_KEY=40 HT_VALUE_BYTES=${bytes}
    HT_BAG_POLICIES=1 HT_WIDE_BAG_POLICIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_LINK_ZERO_SOURCE=1)
  set(check hashtable_design_wide_inline_${bytes}_64_zero_check)
  add_executable(${check} value_width_check.cpp)
  hashtable_migration_settings(${check})
  target_include_directories(${check} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${check} PRIVATE HT_VALUE_BYTES=${bytes} HT_WIDE_INLINE_KEY=64 HT_LINK_ZERO_SOURCE=1)
  add_test(NAME ${check} COMMAND ${check})
endforeach()

# Same wide values and mutation histories, full-value versus selected-field
# consumption. Direct fields isolate transfer cost; they share table routing.
foreach(case IN ITEMS 16_16_1 40_16_1 40_16_2 40_64_1 40_64_2 64_64_2)
  string(REPLACE "_" ";" parameters ${case})
  list(GET parameters 0 key)
  list(GET parameters 1 bytes)
  list(GET parameters 2 kind)
  set(target hashtable_design_hierarchy_packet_${case}_consumer)
  add_executable(${target} hierarchy_batch.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_PACKET_KEY=${key} HT_PACKET_BYTES=${bytes}
    HT_PACKET_KIND=${kind} HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_PACKET_CONSUMERS=1)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()

# External bag append: same geometry/ownership and first-spill path, replacing
# only the live head/hint descriptor after an already-external append.
foreach(bytes IN ITEMS 32 64)
  set(target hashtable_design_wide_bag_40_${bytes}_external)
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_KEY=40 HT_VALUE_BYTES=${bytes}
    HT_BAG_POLICIES=1 HT_WIDE_BAG_POLICIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2 HT_EXTERNAL_BAG_PATCH=1)
  set(check hashtable_design_wide_inline_${bytes}_64_external_check)
  add_executable(${check} value_width_check.cpp)
  hashtable_migration_settings(${check})
  target_include_directories(${check} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${check} PRIVATE HT_VALUE_BYTES=${bytes} HT_WIDE_INLINE_KEY=64 HT_EXTERNAL_BAG_PATCH=1)
  add_test(NAME ${check} COMMAND ${check})
endforeach()
add_executable(hashtable_design_external_patch_check external_patch_check.cpp)
hashtable_migration_settings(hashtable_design_external_patch_check)
target_compile_definitions(hashtable_design_external_patch_check PRIVATE HT_EXTERNAL_BAG_PATCH=1)
add_test(NAME hashtable_design_external_patch_check COMMAND hashtable_design_external_patch_check)
add_executable(hashtable_design_external_narrow_check value_width_check.cpp)
hashtable_migration_settings(hashtable_design_external_narrow_check)
target_include_directories(hashtable_design_external_narrow_check PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_external_narrow_check PRIVATE HT_VALUE_BYTES=2 HT_EXTERNAL_BAG_PATCH=1)
add_test(NAME hashtable_design_external_narrow_check COMMAND hashtable_design_external_narrow_check)

# Existing inline-to-external transition: split-input commit and exact live
# descriptor/tag patch. Keep the preceding external-only control independent.
foreach(bytes IN ITEMS 32 64)
  set(target hashtable_design_wide_bag_40_${bytes}_spill)
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_KEY=40 HT_VALUE_BYTES=${bytes}
    HT_BAG_POLICIES=1 HT_WIDE_BAG_POLICIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
endforeach()
foreach(bytes IN ITEMS 1 2 3 32 57 64)
  set(target hashtable_design_spill_${bytes}_check)
  add_executable(${target} value_width_check.cpp)
  hashtable_migration_settings(${target})
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=${bytes} HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  if(NOT bytes EQUAL 2 AND NOT bytes EQUAL 3)
    target_compile_definitions(${target} PRIVATE HT_WIDE_INLINE_KEY=64)
  endif()
  add_test(NAME ${target} COMMAND ${target})
endforeach()
add_executable(hashtable_design_spill_patch_check external_patch_check.cpp)
hashtable_migration_settings(hashtable_design_spill_patch_check)
target_compile_definitions(hashtable_design_spill_patch_check PRIVATE HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
add_test(NAME hashtable_design_spill_patch_check COMMAND hashtable_design_spill_patch_check)

# A 64-byte MM inline budget needs a 65-byte tagged key-overflow row. Measure
# bucket count/unit, inline budget and storage together, including both baselines.
foreach(key IN ITEMS 16 40 64)
  foreach(bytes IN ITEMS 32 64)
    set(target hashtable_design_wide_bag_${key}_${bytes})
    add_executable(${target} value_width_bench.cpp)
    hashtable_migration_settings(${target})
    target_compile_definitions(${target} PRIVATE HT_VALUE_KEY=${key} HT_VALUE_BYTES=${bytes}
      HT_BAG_POLICIES=1 HT_WIDE_BAG_POLICIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2)
  endforeach()
endforeach()
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  set(target hashtable_design_wide_inline_64_${key}_check)
  add_executable(${target} value_width_check.cpp)
  hashtable_migration_settings(${target})
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=64 HT_WIDE_INLINE_KEY=${key})
  add_test(NAME ${target} COMMAND ${target})
endforeach()
foreach(bytes IN ITEMS 1 32 57 63)
  set(target hashtable_design_wide_inline_${bytes}_40_check)
  add_executable(${target} value_width_check.cpp)
  hashtable_migration_settings(${target})
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=${bytes} HT_WIDE_INLINE_KEY=40)
  add_test(NAME ${target} COMMAND ${target})
endforeach()

# Resumed architecture: contiguous pooled vectors vs chains and direct buffers.
foreach(width IN ITEMS 2 8 32 64)
  set(target hashtable_design_vectors_${width})
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=${width} HT_VALUE_KEY=40
    HT_BAG_POLICIES=1 HT_VECTOR_BAG_POLICIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1
    HT_HASH_MASK=2 HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
endforeach()
add_executable(hashtable_design_vector_check vector_check.cpp)
hashtable_migration_settings(hashtable_design_vector_check)
target_compile_definitions(hashtable_design_vector_check PRIVATE HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
add_test(NAME hashtable_design_vector_check COMMAND hashtable_design_vector_check)

foreach(width IN ITEMS 8 64)
  set(target hashtable_design_vectors_reserved_${width})
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_BYTES=${width} HT_BAG_POLICIES=1
    HT_VECTOR_BAG_POLICIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1 HT_LINK_RESERVE_BLOCK=1)
endforeach()
add_executable(hashtable_design_vector_reserved_check vector_check.cpp)
hashtable_migration_settings(hashtable_design_vector_reserved_check)
target_compile_definitions(hashtable_design_vector_reserved_check PRIVATE HT_EXTERNAL_BAG_PATCH=1
  HT_BAG_SPILL_PATCH=1 HT_LINK_RESERVE_BLOCK=1)
add_test(NAME hashtable_design_vector_reserved_check COMMAND hashtable_design_vector_reserved_check)

add_executable(hashtable_design_vector_publication small_publication.cpp)
hashtable_migration_settings(hashtable_design_vector_publication)
target_include_directories(hashtable_design_vector_publication PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_vector_publication PRIVATE HT_PUBLICATION_VECTOR_BAGS=1 HT_PUBLICATION_INLINE=2)
add_test(NAME hashtable_design_vector_publication COMMAND hashtable_design_vector_publication check)

# Generic owner and its real logical values through direct/manual/Loom consumers.
foreach(kind IN ITEMS 0 1 2)
  set(target hashtable_design_staged_${kind})
  add_executable(${target} staged.cpp)
  hashtable_migration_settings(${target})
  target_link_libraries(${target} PRIVATE loom::loom)
  target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  add_test(NAME ${target}_check COMMAND ${target} check)
endforeach()

foreach(kind IN ITEMS 0 1 2)
  set(target hashtable_design_staged_forced_${kind})
  add_executable(${target} staged.cpp)
  hashtable_migration_settings(${target})
  target_link_libraries(${target} PRIVATE loom::loom)
  target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_STAGED_A=3 HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  add_test(NAME ${target}_check COMMAND ${target} check)
endforeach()

foreach(bags IN ITEMS 1 2)
  set(target hashtable_design_staged_owner_${bags})
  add_executable(${target} staged.cpp)
  hashtable_migration_settings(${target})
  target_link_libraries(${target} PRIVATE loom::loom)
  target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=2 HT_STAGED_A=3 HT_STAGED_BAGS=${bags}
    HT_HASH_MASK=2 HT_RECIPE_SCAN32=1 HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  add_test(NAME ${target}_check COMMAND ${target} check)
endforeach()

# Different complete owner footprints; these names do not assert cache residence.
foreach(scale IN ITEMS hot large dram)
  foreach(kind IN ITEMS 0 1 2)
    set(a 10)
    set(n 6144)
    if(scale STREQUAL "large")
      set(a 19)
      set(n 3600000)
    elseif(scale STREQUAL "dram")
      set(a 23)
      set(n 57600000)
    endif()
    set(unit 128)
    if(kind EQUAL 0)
      set(unit 64)
      if(scale STREQUAL "dram")
        # Compressed sets need twice as many keys to exceed Granite's large LLC.
        set(n 115200000)
      elseif(scale STREQUAL "large")
        set(a 20)
        set(n 14400000)
      else()
        math(EXPR a "${a}-1")
      endif()
    elseif(kind EQUAL 2)
      set(unit 192)
    endif()
    set(target hashtable_design_staged_${scale}_${kind})
    add_executable(${target} staged.cpp)
    hashtable_migration_settings(${target})
    target_link_libraries(${target} PRIVATE loom::loom)
    target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_STAGED_A=${a}
      HT_STAGED_UNIT=${unit} HT_STAGED_N=${n} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
      HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
    if(NOT scale STREQUAL "dram")
      set(target hashtable_design_staged_${scale}_${kind}_graph)
      add_executable(${target} staged.cpp)
      hashtable_migration_settings(${target})
      target_link_libraries(${target} PRIVATE loom::loom)
      target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_STAGED_A=${a}
        HT_STAGED_UNIT=${unit} HT_STAGED_N=${n} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
        HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1 HT_STAGED_GRAPH_HINT=1)
    endif()
  endforeach()
endforeach()

# Executable boundary coverage for every key width and narrow/wide consumers.
foreach(spec IN ITEMS "8,1,1" "16,2,2" "24,3,1" "32,4,2" "40,57,2" "48,16,1" "56,32,2" "64,64,1")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  list(GET parts 2 kind)
  set(target hashtable_design_staged_width_${key})
  add_executable(${target} staged.cpp)
  hashtable_migration_settings(${target})
  target_link_libraries(${target} PRIVATE loom::loom)
  target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_STAGED_KEY=${key}
    HT_STAGED_WIDTH=${width} HT_STAGED_A=3 HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  add_test(NAME ${target}_check COMMAND ${target} check)
endforeach()

# Expose the secondary capacity/unit choices held fixed by the previous sweep.
foreach(spec IN ITEMS "16,2,1" "40,16,1" "64,16,1" "40,8,2")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  list(GET parts 2 kind)
  set(target hashtable_design_coupled_${key}_${width}_${kind})
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_KEY=${key} HT_VALUE_BYTES=${width}
    HT_GEOMETRY_KIND=${kind} HT_GEOMETRY_POLICIES=1 HT_COUPLED_GEOMETRY=1
    HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
endforeach()

# Capacity families use the same complete-operation driver and solver. The
# cases span every hierarchy key domain and sets/narrow/wide maps and bags;
# K8 keeps the separately qualified compact whole-table-list regime.
foreach(spec IN ITEMS "16,0,0" "24,3,1" "32,4,1" "40,0,0" "40,2,1"
    "48,16,2" "56,32,1" "64,16,1" "64,64,1" "40,2,2" "64,64,2")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  list(GET parts 2 kind)
  set(target hashtable_design_capacity_${key}_${width}_${kind})
  add_executable(${target} value_width_bench.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_VALUE_KEY=${key} HT_VALUE_BYTES=${width}
    HT_GEOMETRY_KIND=${kind} HT_GEOMETRY_POLICIES=1 HT_COUPLED_GEOMETRY=1
    HT_CAPACITY_FAMILIES=1 HT_OCCUPANCY_STUDY=1 HT_RECIPE_SCAN32=1 HT_HASH_MASK=2
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
endforeach()

add_executable(hashtable_design_staged_publication staged_publication.cpp)
hashtable_migration_settings(hashtable_design_staged_publication)
target_link_libraries(hashtable_design_staged_publication PRIVATE loom::loom)
target_include_directories(hashtable_design_staged_publication PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_staged_publication PRIVATE HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
  HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
add_test(NAME hashtable_design_staged_publication COMMAND hashtable_design_staged_publication)

# Natural single-writer growth through the same solved families. Slot count,
# doubling/quadrupling and load are experimental controls, not owner API knobs.
foreach(spec IN ITEMS "8,0,0" "16,0,0" "24,3,1" "32,4,1" "40,0,0" "40,2,1"
    "48,16,2" "56,32,1" "64,64,1" "40,2,2" "64,64,2")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  list(GET parts 2 kind)
  foreach(slots IN ITEMS 14 28)
    foreach(step IN ITEMS 1 2)
      foreach(load IN ITEMS 80 90 100)
        set(target hashtable_design_cycle_${key}_${width}_${kind}_${slots}_${step}_${load})
        add_executable(${target} growth_cycle.cpp)
        hashtable_migration_settings(${target})
        target_compile_definitions(${target} PRIVATE HT_CYCLE_KEY=${key} HT_CYCLE_WIDTH=${width}
          HT_CYCLE_KIND=${kind} HT_CYCLE_SLOTS=${slots} HT_CYCLE_STEP=${step} HT_CYCLE_LOAD=${load}
          HT_HASH_MASK=2 HT_RECIPE_SCAN32=1 HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
        add_test(NAME ${target}_check COMMAND ${target} check)
      endforeach()
    endforeach()
  endforeach()
endforeach()

# Attribution control for allocator perturbation by validation between lifetime
# phases. Both modes verify full values/multiplicity; only one allocates then.
foreach(spec IN ITEMS "40,0,0" "40,2,1" "40,2,2" "64,64,1" "64,64,2")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  list(GET parts 2 kind)
  foreach(allocating IN ITEMS 0 1)
    set(target hashtable_design_cycle_oracle_${key}_${width}_${kind}_${allocating})
    add_executable(${target} growth_cycle.cpp)
    hashtable_migration_settings(${target})
    target_compile_definitions(${target} PRIVATE HT_CYCLE_KEY=${key} HT_CYCLE_WIDTH=${width}
      HT_CYCLE_KIND=${kind} HT_CYCLE_SLOTS=14 HT_CYCLE_STEP=1 HT_CYCLE_LOAD=90
      HT_CYCLE_ALLOCATING_ORACLE=${allocating} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
      HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  endforeach()
endforeach()

# Same physical capacity family, varied read windows/staging. Reference bucket
# levels describe size, not cache residence. Build only the requested cases.
foreach(reference IN ITEMS 10 18 21 25)
  foreach(spec IN ITEMS "40,0,0" "40,2,1" "64,64,1" "40,2,2" "64,64,2")
    string(REPLACE "," ";" parts "${spec}")
    list(GET parts 0 key)
    list(GET parts 1 width)
    list(GET parts 2 kind)
    foreach(slots IN ITEMS 14 28)
      if(slots EQUAL 14)
        math(EXPR a "${reference}-1")
      else()
        math(EXPR a "${reference}-2")
      endif()
      foreach(windows IN ITEMS 0 1 2)
        set(target hashtable_design_staged_windows_a${reference}_${key}_${width}_${kind}_${slots}_${windows})
        add_executable(${target} staged.cpp)
        hashtable_migration_settings(${target})
        target_link_libraries(${target} PRIVATE loom::loom)
        target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_STAGED_KEY=${key}
          HT_STAGED_WIDTH=${width} HT_STAGED_A=${a} HT_STAGED_CAPACITY=${slots} HT_RECORD_READ_WINDOWS=${windows}
          HT_STAGED_NO_WORD=1 HT_STAGED_GRAPH_HINT=1 HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
          HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
        add_test(NAME ${target}_check COMMAND ${target} check)
      endforeach()
    endforeach()
  endforeach()
endforeach()

foreach(spec IN ITEMS "8,1,1" "16,2,2" "24,3,1" "32,4,2" "40,57,2" "48,16,1" "56,32,2" "64,64,1")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  list(GET parts 2 kind)
  set(target hashtable_design_staged_windows_width_${key})
  add_executable(${target} staged.cpp)
  hashtable_migration_settings(${target})
  target_link_libraries(${target} PRIVATE loom::loom)
  target_compile_definitions(${target} PRIVATE HT_STAGED_KIND=${kind} HT_STAGED_KEY=${key}
    HT_STAGED_WIDTH=${width} HT_STAGED_A=3 HT_RECORD_READ_WINDOWS=2 HT_STAGED_NO_WORD=1
    HT_STAGED_GRAPH_HINT=1 HT_HASH_MASK=2 HT_RECIPE_SCAN32=1 HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  add_test(NAME ${target}_check COMMAND ${target} check)
endforeach()
add_executable(hashtable_design_staged_windows_publication staged_publication.cpp)
hashtable_migration_settings(hashtable_design_staged_windows_publication)
target_link_libraries(hashtable_design_staged_windows_publication PRIVATE loom::loom)
target_include_directories(hashtable_design_staged_windows_publication PRIVATE ${PROJECT_SOURCE_DIR}/orbital/src)
target_compile_definitions(hashtable_design_staged_windows_publication PRIVATE HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
  HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1 HT_RECORD_READ_WINDOWS=2)
add_test(NAME hashtable_design_staged_windows_publication COMMAND hashtable_design_staged_windows_publication)

# Cross-Pool consolidation: complete operations and ordinary/exact baselines in
# each prototype. Bounded value groups keep multimap transfer out of scalar-key
# replay. Small whole-table lists and hierarchy bodies are both measured.
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(spec IN ITEMS "0,0" "1,2" "1,64" "2,2" "2,64")
    string(REPLACE "," ";" parts "${spec}")
    list(GET parts 0 kind)
    list(GET parts 1 width)
    set(target hashtable_design_consolidation_${key}_${kind}_${width})
    add_executable(${target} consolidation.cpp)
    hashtable_migration_settings(${target})
    target_compile_definitions(${target} PRIVATE HT_MERGE_KEY=${key}
      HT_MERGE_KIND=${kind} HT_MERGE_WIDTH=${width} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
    add_test(NAME ${target} COMMAND ${target} check)
  endforeach()
endforeach()

# Preserve the unpatched construction control while measuring the existing
# qualified spill/external mutation bodies in the same complete merge history.
foreach(spec IN ITEMS "8,64" "40,2" "64,64")
  string(REPLACE "," ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 width)
  set(target hashtable_design_consolidation_${key}_2_${width}_patch)
  add_executable(${target} consolidation.cpp)
  hashtable_migration_settings(${target})
  target_compile_definitions(${target} PRIVATE HT_MERGE_KEY=${key} HT_MERGE_KIND=2
    HT_MERGE_WIDTH=${width} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1
    HT_EXTERNAL_BAG_PATCH=1 HT_BAG_SPILL_PATCH=1)
  add_test(NAME ${target} COMMAND ${target} check)
endforeach()

# Odd tails and intermediate inline/external boundaries for consolidation.
foreach(width IN ITEMS 1 3 7 8 16 32)
  foreach(kind IN ITEMS 1 2)
    set(target hashtable_design_consolidation_40_${kind}_${width})
    add_executable(${target} consolidation.cpp)
    hashtable_migration_settings(${target})
    target_compile_definitions(${target} PRIVATE HT_MERGE_KEY=40
      HT_MERGE_KIND=${kind} HT_MERGE_WIDTH=${width} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
    add_test(NAME ${target} COMMAND ${target} check)
  endforeach()
endforeach()

# Bounded actual-Worker intake and two coordinated encoding changes. Split TUs
# by key/kind/value width so breadth does not require one enormous compilation.
foreach(key IN ITEMS 8 16 24 32 40 48 56 64)
  foreach(spec IN ITEMS "0,0" "1,2" "1,64" "2,2" "2,64")
    string(REPLACE "," ";" parts "${spec}")
    list(GET parts 0 kind)
    list(GET parts 1 width)
    set(target hashtable_design_worker_epochs_${key}_${kind}_${width})
    add_executable(${target} worker_epochs.cpp)
    hashtable_migration_settings(${target})
    target_link_libraries(${target} PRIVATE loom::loom)
    target_compile_definitions(${target} PRIVATE HT_EPOCH_KEY=${key}
      HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
    add_test(NAME ${target} COMMAND ${target} check)
    foreach(policy IN ITEMS 1 2 3 4 5)
      # Growth/layout attribution controls, not four public owner modes.
      if((policy EQUAL 2 OR policy EQUAL 3) AND NOT kind EQUAL 2)
        continue()
      endif()
      set(variant ${target}_policy${policy})
      add_executable(${variant} worker_epochs.cpp)
      hashtable_migration_settings(${variant})
      target_link_libraries(${variant} PRIVATE loom::loom)
      target_compile_definitions(${variant} PRIVATE HT_EPOCH_KEY=${key}
        HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_EPOCH_POLICY=${policy}
        HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
      add_test(NAME ${variant} COMMAND ${variant} check)
    endforeach()
    # The actual Worker callback pays packet preparation and prefix retry.
    # These are write-lowering controls over policy5, not owner-format modes.
    foreach(packet IN ITEMS 1 2)
      set(variant ${target}_policy5_packet${packet})
      add_executable(${variant} worker_epochs.cpp)
      hashtable_migration_settings(${variant})
      target_link_libraries(${variant} PRIVATE loom::loom)
      if(packet EQUAL 2)
        set(small_scalar 1)
      else()
        set(small_scalar 0)
      endif()
      target_compile_definitions(${variant} PRIVATE HT_EPOCH_KEY=${key}
        HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_EPOCH_POLICY=5
        HT_EPOCH_PACKET=${packet} HT_SMALL_PACKET_SCALAR=${small_scalar}
        HT_HIERARCHY_PACKET_HASH=${packet} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
      add_test(NAME ${variant} COMMAND ${variant} check)
      set(lookup ${variant}_lookup)
      add_executable(${lookup} worker_epochs.cpp)
      hashtable_migration_settings(${lookup})
      target_link_libraries(${lookup} PRIVATE loom::loom)
      target_compile_definitions(${lookup} PRIVATE HT_EPOCH_KEY=${key}
        HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_EPOCH_POLICY=5
        HT_EPOCH_PACKET=${packet} HT_SMALL_PACKET_SCALAR=${small_scalar}
        HT_SMALL_PACKET_LOOKUP=1 HT_HIERARCHY_PACKET_HASH=${packet}
        HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
      add_test(NAME ${lookup} COMMAND ${lookup} check)
    endforeach()
    # Carry the measured doubling/90% choice into the complete Worker caller.
    # Compare conservative pending bounds with completed occupancy deltas.
    foreach(growth IN ITEMS 1 2)
      set(variant ${target}_policy5_packet2_growth${growth})
      add_executable(${variant} worker_epochs.cpp)
      hashtable_migration_settings(${variant})
      target_link_libraries(${variant} PRIVATE loom::loom)
      target_compile_definitions(${variant} PRIVATE HT_EPOCH_KEY=${key}
        HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_EPOCH_POLICY=5
        HT_EPOCH_PACKET=2 HT_SMALL_PACKET_SCALAR=1 HT_HIERARCHY_PACKET_HASH=2
        HT_EPOCH_GROWTH=${growth} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
      add_test(NAME ${variant} COMMAND ${variant} check)
    endforeach()
    set(fold hashtable_design_worker_fold_${key}_${kind}_${width})
    add_executable(${fold} worker_fold.cpp)
    hashtable_migration_settings(${fold})
    target_link_libraries(${fold} PRIVATE loom::loom)
    if(width GREATER 8 AND kind EQUAL 1)
      set(fold_slots 14)
    else()
      set(fold_slots 0)
    endif()
    target_compile_definitions(${fold} PRIVATE HT_EPOCH_KEY=${key}
      HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_EPOCH_POLICY=5
      HT_EPOCH_PACKET=2 HT_SMALL_PACKET_SCALAR=1 HT_HIERARCHY_PACKET_HASH=2
      HT_EPOCH_GROWTH=2 HT_EPOCH_SLOTS=${fold_slots} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
    add_test(NAME ${fold} COMMAND ${fold} check)
    foreach(slots IN ITEMS 14 28)
      set(variant ${target}_policy5_packet2_growth2_slots${slots})
      add_executable(${variant} worker_epochs.cpp)
      hashtable_migration_settings(${variant})
      target_link_libraries(${variant} PRIVATE loom::loom)
      target_compile_definitions(${variant} PRIVATE HT_EPOCH_KEY=${key}
        HT_EPOCH_KIND=${kind} HT_EPOCH_WIDTH=${width} HT_EPOCH_POLICY=5
        HT_EPOCH_PACKET=2 HT_SMALL_PACKET_SCALAR=1 HT_HIERARCHY_PACKET_HASH=2
        HT_EPOCH_GROWTH=2 HT_EPOCH_SLOTS=${slots} HT_HASH_MASK=2 HT_RECIPE_SCAN32=1)
      add_test(NAME ${variant} COMMAND ${variant} check)
    endforeach()
  endforeach()
endforeach()

# Frozen controls used by the retained comparisons; not another selected architecture.
add_subdirectory(reference EXCLUDE_FROM_ALL)
