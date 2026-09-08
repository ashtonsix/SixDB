include(FetchContent)
# Standalone RE2 revision: one parser supplies both the AST and the oracle.
# Internal AST use is confined to this spike; this is not a SixDB dependency pin.
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(regexp_re2
  URL https://codeload.github.com/google/re2/tar.gz/a44f1bebafdc36437643ba168b839611cb15d37d
  URL_HASH SHA256=f108393fc0d28fb3d0769a8588d56888c0267fb23fef84777baba0e974097df8)
FetchContent_MakeAvailable(regexp_re2)
add_library(regexp_lowering_core STATIC lower.cpp factor.cpp)
sixdb_target(regexp_lowering_core)
target_include_directories(regexp_lowering_core SYSTEM PUBLIC ${regexp_re2_SOURCE_DIR})
target_link_libraries(regexp_lowering_core PUBLIC re2)
foreach(driver check probe factor_check factor_probe)
  add_executable(regexp_lowering_${driver} ${driver}.cpp)
  sixdb_target(regexp_lowering_${driver})
  target_link_libraries(regexp_lowering_${driver} PRIVATE regexp_lowering_core)
endforeach()
