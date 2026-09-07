include_guard(GLOBAL)
include(FetchContent)

# First working pin, independent of Calico's checkout and build artifacts.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googlebenchmark
  URL https://codeload.github.com/google/benchmark/tar.gz/eddb0241389718a23a42db6af5f0164b6e0139af
  URL_HASH SHA256=5580831930bafe41ecb74f2af47d9adcec25d5bb02c30228d37b7032e0aa6122
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googlebenchmark)
# SixDB retains assertions in optimized builds; the benchmark library uses
# NDEBUG to select its own optimized runtime and report its build type.
target_compile_definitions(benchmark PRIVATE
  "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NDEBUG>")
