include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")

# `current` is the maintained NEON SeriesPack implementation; every other
# variant is a patch against it (`original` restores the XOR delta-swap
# transpose and pair packets, `bit4` keeps only the BIT transpose, `intreg`
# restores integer-register mixed residual reads, `bitsel` emits BIT for the
# constant-mask selects and masked ORs of striped tails). `mixed4w.patch` is a
# rejected record against the pre-`intreg` source and is not built. The
# patched copies form a build-private overlay searched before ikea/include, and
# each variant compiles its own SeriesPack library objects: sharing one library
# would let the linker keep a single copy of each inline template instantiation.
set(ikea_dir "${PROJECT_SOURCE_DIR}/ikea")
set(neon_headers detail/native/neon/read.h detail/native/neon/groups.h
  detail/native/neon/write.h detail/dense_read.h detail/placed_read.h author/expression.h)
find_program(NEON_LOCAL_TRANSPOSE_PATCH patch REQUIRED)
set(variants current bit4 original intreg bitsel)
foreach(variant IN LISTS variants)
  set(overlay "${CMAKE_CURRENT_BINARY_DIR}/overlay-${variant}")
  set(staging "${CMAKE_CURRENT_BINARY_DIR}/staging-${variant}")
  set(neon_dir "ikea/seriespack/detail/native/neon")
  file(REMOVE_RECURSE "${staging}")
  file(MAKE_DIRECTORY "${staging}/${neon_dir}" "${overlay}/${neon_dir}"
    "${staging}/ikea/seriespack/author" "${overlay}/ikea/seriespack/author")
  foreach(header IN LISTS neon_headers)
    set(path "ikea/seriespack/${header}")
    file(COPY_FILE "${ikea_dir}/include/${path}" "${staging}/${path}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ikea_dir}/include/${path}")
  endforeach()
  if(NOT variant STREQUAL "current")
    set(patch_file "${CMAKE_CURRENT_SOURCE_DIR}/${variant}.patch")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch_file}")
    execute_process(COMMAND "${NEON_LOCAL_TRANSPOSE_PATCH}" -p1 --forward --silent -d "${staging}"
      -i "${patch_file}" RESULT_VARIABLE failed)
    if(failed)
      message(FATAL_ERROR "neon-local-transpose: ${variant}.patch no longer applies")
    endif()
  endif()
  # Copy only changed headers, so reconfiguring does not rebuild every variant.
  foreach(header IN LISTS neon_headers)
    set(path "ikea/seriespack/${header}")
    file(COPY_FILE "${staging}/${path}" "${overlay}/${path}" ONLY_IF_DIFFERENT)
  endforeach()

  foreach(scope bench full)
    if(scope STREQUAL "bench")
      set(sources admission.cpp representation.cpp)
    else()
      set(sources readers.cpp admission.cpp representation.cpp)
    endif()
    list(TRANSFORM sources PREPEND "${ikea_dir}/src/seriespack/")
    set(library neon_local_transpose_sp_${scope}_${variant})
    add_library(${library} STATIC EXCLUDE_FROM_ALL ${sources})
    sixdb_target(${library})
    target_include_directories(${library} BEFORE PUBLIC "${overlay}")
    target_include_directories(${library} PUBLIC "${ikea_dir}/include")
    target_compile_features(${library} PUBLIC cxx_std_23)
    target_link_libraries(${library} PUBLIC ikea_core)
  endforeach()

  add_executable(neon_local_transpose_bench_${variant} bench.cpp)
  sixdb_target(neon_local_transpose_bench_${variant})
  target_compile_definitions(neon_local_transpose_bench_${variant} PRIVATE
    NEON_LOCAL_TRANSPOSE_VARIANT="${variant}")
  target_link_libraries(neon_local_transpose_bench_${variant} PRIVATE
    neon_local_transpose_sp_bench_${variant} benchmark::benchmark)

  # The maintained SeriesPack check, built against this overlay.
  set(check_sources support.cpp read/wire.cpp read/composition.cpp mutation/range.cpp
    mutation/substitution.cpp execution/chain.cpp mutation/construction.cpp
    mutation/widths_1_8.cpp mutation/widths_9_16.cpp mutation/widths_17_24.cpp
    mutation/widths_25_32.cpp mutation/widths_33_40.cpp mutation/widths_41_48.cpp
    mutation/widths_49_56.cpp mutation/widths_57_64.cpp mutation/dense.cpp mutation/verify.cpp)
  list(TRANSFORM check_sources PREPEND "${ikea_dir}/test/seriespack/")
  add_executable(neon_local_transpose_seriespack_check_${variant} EXCLUDE_FROM_ALL ${check_sources})
  sixdb_target(neon_local_transpose_seriespack_check_${variant})
  target_link_libraries(neon_local_transpose_seriespack_check_${variant} PRIVATE
    neon_local_transpose_sp_full_${variant})

  # Preset follow-up: Local versus striped (preset.sh on a worker, preset_summary.py
  # on its results); with run.py --binary also striped kernels across variants.
  add_executable(neon_local_transpose_preset_bench_${variant} EXCLUDE_FROM_ALL preset.cpp)
  sixdb_target(neon_local_transpose_preset_bench_${variant})
  target_link_libraries(neon_local_transpose_preset_bench_${variant} PRIVATE
    neon_local_transpose_sp_bench_${variant} benchmark::benchmark)

  add_executable(neon_local_transpose_check_${variant} EXCLUDE_FROM_ALL check.cpp)
  sixdb_target(neon_local_transpose_check_${variant})
  target_compile_definitions(neon_local_transpose_check_${variant} PRIVATE
    NEON_LOCAL_TRANSPOSE_VARIANT="${variant}")
  target_link_libraries(neon_local_transpose_check_${variant} PRIVATE
    neon_local_transpose_sp_bench_${variant})
endforeach()

