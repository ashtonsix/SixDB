# Configure only requested suites/studies; build only requested targets.
add_subdirectory(benchmarks)
add_subdirectory(spikes)
add_subdirectory(simulator EXCLUDE_FROM_ALL)
