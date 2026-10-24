# Prototype 1: the stage contract with runtime-composed block-grain laws.
# Width-specialised SeriesPack arrays compile once, in their own TUs.
# FRAME3_PRESETS limits the width TUs (each ~4-7 GB to compile) to the
# presets a build needs, e.g. -DFRAME3_PRESETS=compact for local checks.
set(FRAME3_PRESETS "compact;bulk_x86;bulk_arm" CACHE STRING "SeriesPack presets to instantiate width arrays for")
# FRAME3_LEAF=runtime uses SeriesPack's compiled-once runtime-width operations
# for every width and preset, and compiles no width TUs at all.
set(FRAME3_LEAF "runtime" CACHE STRING "typed (width TUs) or runtime (SeriesPack compiled-once operations)")
if(FRAME3_LEAF STREQUAL "runtime")
    set(FRAME3_PRESETS "")
endif()
set(frame3_width_sources widths.cpp)
foreach(preset compact bulk_x86 bulk_arm)
    if(preset IN_LIST FRAME3_PRESETS)
        set(frame3_has_${preset} 1)
        foreach(first 1 17 33 49)
            list(APPEND frame3_width_sources widths_${preset}_${first}.cpp)
        endforeach()
    else()
        set(frame3_has_${preset} 0)
    endif()
endforeach()
add_library(frame3_widths STATIC EXCLUDE_FROM_ALL ${frame3_width_sources})
target_compile_definitions(frame3_widths PRIVATE FRAME3_HAS_COMPACT=${frame3_has_compact}
    FRAME3_HAS_BULK_X86=${frame3_has_bulk_x86} FRAME3_HAS_BULK_ARM=${frame3_has_bulk_arm}
    FRAME3_RUNTIME_LEAF=$<STREQUAL:${FRAME3_LEAF},runtime>)
sixdb_target(frame3_widths)
target_link_libraries(frame3_widths PUBLIC ikea::seriespack)
add_executable(frame3_check check.cpp)
sixdb_target(frame3_check)
target_link_libraries(frame3_check PRIVATE frame3_widths ikea::tuplepack)
add_executable(frame3_measure measure.cpp)
sixdb_target(frame3_measure)
target_link_libraries(frame3_measure PRIVATE frame3_widths ikea::tuplepack)
add_executable(frame3_writes writes.cpp)
sixdb_target(frame3_writes)
target_link_libraries(frame3_writes PRIVATE frame3_widths ikea::tuplepack)
add_executable(frame3_calibrate calibrate.cpp)
sixdb_target(frame3_calibrate)
target_link_libraries(frame3_calibrate PRIVATE frame3_widths ikea::tuplepack)
add_executable(frame3_e2e e2e.cpp orbital_frame.cpp)
sixdb_target(frame3_e2e)
target_link_libraries(frame3_e2e PRIVATE frame3_widths ikea::tuplepack orbital::orbital)
# Seam cases: front A's native controls (phase 2 instruments) and Loom's Ring.
if(TARGET frame_cells_controls)
    add_executable(frame3_seams seams.cpp)
    sixdb_target(frame3_seams)
    target_link_libraries(frame3_seams PRIVATE frame3_widths ikea::tuplepack frame_cells_controls loom)
endif()
