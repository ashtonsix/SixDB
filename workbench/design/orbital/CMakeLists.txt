# Declaration-only design: compile the boundaries/callers without a fake runtime.
add_library(orbital_design OBJECT EXCLUDE_FROM_ALL
  src/object.cpp src/journal.cpp src/port.cpp src/executor.cpp src/interfaces.cpp)
target_include_directories(orbital_design PUBLIC include)
target_link_libraries(orbital_design PUBLIC orbital::orbital)
sixdb_target(orbital_design)

add_library(loom_design OBJECT EXCLUDE_FROM_ALL src/loom.cpp)
target_link_libraries(loom_design PUBLIC orbital_design loom::loom)
sixdb_target(loom_design)

set(orbital_design_examples)
foreach(example IN ITEMS caller native jobs partitions extensions)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/examples/${example}.md")
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/examples/${example}.cpp")
  add_custom_command(OUTPUT "${generated}"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/examples/extract.py" "${source}" "${generated}"
    DEPENDS "${source}" examples/extract.py
    COMMENT "Extracting ${example} literate example"
    VERBATIM)
  list(APPEND orbital_design_examples "${generated}")
endforeach()

add_library(orbital_design_callers OBJECT EXCLUDE_FROM_ALL ${orbital_design_examples})
target_link_libraries(orbital_design_callers PRIVATE orbital_design loom::loom)
sixdb_target(orbital_design_callers)

add_custom_target(orbital_design_check
  DEPENDS orbital_design loom_design orbital_design_callers)
