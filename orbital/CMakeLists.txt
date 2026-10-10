find_package(Threads REQUIRED)
add_library(orbital STATIC
  src/types.cpp src/worker.cpp src/object/storage.cpp
  src/object/representation.cpp
  src/allocation/allocation.cpp src/allocation/arena.cpp
  src/allocation/group.cpp src/allocation/window.cpp src/allocation/finite.cpp)
add_library(orbital::orbital ALIAS orbital)
sixdb_target(orbital)
target_include_directories(orbital PUBLIC include)
target_link_libraries(orbital PUBLIC Threads::Threads)
add_executable(orbital_allocation_check EXCLUDE_FROM_ALL test/allocation.cpp)
sixdb_target(orbital_allocation_check)
target_link_libraries(orbital_allocation_check PRIVATE orbital)
add_test(NAME orbital_allocation_check COMMAND orbital_allocation_check)
add_executable(orbital_object_check EXCLUDE_FROM_ALL test/object.cpp test/object_material.cpp)
sixdb_target(orbital_object_check)
target_include_directories(orbital_object_check PRIVATE src)
target_link_libraries(orbital_object_check PRIVATE orbital)
add_test(NAME orbital_object_check COMMAND orbital_object_check)
set_tests_properties(orbital_object_check PROPERTIES TIMEOUT 30)
add_executable(orbital_representation_check EXCLUDE_FROM_ALL test/representation.cpp)
sixdb_target(orbital_representation_check)
target_include_directories(orbital_representation_check PRIVATE src)
target_link_libraries(orbital_representation_check PRIVATE orbital Threads::Threads)
add_test(NAME orbital_representation_check COMMAND orbital_representation_check)
set_tests_properties(orbital_representation_check PROPERTIES TIMEOUT 30)
add_executable(orbital_finite_check EXCLUDE_FROM_ALL test/finite.cpp)
sixdb_target(orbital_finite_check)
target_include_directories(orbital_finite_check PRIVATE src/allocation)
target_link_libraries(orbital_finite_check PRIVATE orbital)
add_test(NAME orbital_finite_check COMMAND orbital_finite_check)
add_executable(orbital_reclamation_check EXCLUDE_FROM_ALL test/reclamation.cpp)
sixdb_target(orbital_reclamation_check)
target_include_directories(orbital_reclamation_check PRIVATE src/allocation)
target_link_libraries(orbital_reclamation_check PRIVATE orbital)
target_link_options(orbital_reclamation_check PRIVATE
  -Wl,--wrap=mmap,--wrap=madvise,--wrap=mprotect,--wrap=munmap,--wrap=_ZnwmRKSt9nothrow_t)
add_test(NAME orbital_reclamation_check COMMAND orbital_reclamation_check)
add_executable(orbital_window_check EXCLUDE_FROM_ALL test/window.cpp)
sixdb_target(orbital_window_check)
target_include_directories(orbital_window_check PRIVATE src/allocation)
target_link_libraries(orbital_window_check PRIVATE orbital)
target_link_options(orbital_window_check PRIVATE -Wl,--wrap=mprotect)
add_test(NAME orbital_window_check COMMAND orbital_window_check)
add_executable(orbital_return_check EXCLUDE_FROM_ALL test/returns.cpp)
sixdb_target(orbital_return_check)
target_link_libraries(orbital_return_check PRIVATE orbital)
target_link_options(orbital_return_check PRIVATE -Wl,--wrap=pthread_mutex_lock,--wrap=mmap)
add_test(NAME orbital_return_check COMMAND orbital_return_check)
add_executable(orbital_worker_check EXCLUDE_FROM_ALL test/worker.cpp)
sixdb_target(orbital_worker_check)
target_link_libraries(orbital_worker_check PRIVATE orbital)
add_test(NAME orbital_worker_check COMMAND orbital_worker_check)
add_executable(orbital_lifetime_check EXCLUDE_FROM_ALL test/lifetime.cpp)
sixdb_target(orbital_lifetime_check)
target_link_libraries(orbital_lifetime_check PRIVATE orbital)
target_link_options(orbital_lifetime_check PRIVATE -Wl,--wrap=munmap)
add_test(NAME orbital_lifetime_check COMMAND orbital_lifetime_check)
add_executable(orbital_thread_check EXCLUDE_FROM_ALL test/thread.cpp)
sixdb_target(orbital_thread_check)
target_link_libraries(orbital_thread_check PRIVATE orbital)
target_link_options(orbital_thread_check PRIVATE -Wl,--wrap=munmap)
add_test(NAME orbital_thread_check COMMAND orbital_thread_check)
add_executable(orbital_preparation_check EXCLUDE_FROM_ALL test/preparation.cpp)
sixdb_target(orbital_preparation_check)
target_include_directories(orbital_preparation_check PRIVATE src/allocation)
target_link_libraries(orbital_preparation_check PRIVATE orbital ikea::seriespack ikea::tuplepack)
target_link_options(orbital_preparation_check PRIVATE
  -Wl,--wrap=mmap,--wrap=mprotect,--wrap=munmap,--wrap=_ZnwmRKSt9nothrow_t)
add_test(NAME orbital_preparation_check COMMAND orbital_preparation_check)
add_executable(orbital_notification_check EXCLUDE_FROM_ALL test/notification.cpp)
sixdb_target(orbital_notification_check)
target_link_libraries(orbital_notification_check PRIVATE orbital)
add_test(NAME orbital_notification_check COMMAND orbital_notification_check)
add_executable(orbital_budget_check EXCLUDE_FROM_ALL test/budget.cpp)
sixdb_target(orbital_budget_check)
target_link_libraries(orbital_budget_check PRIVATE orbital)
target_link_options(orbital_budget_check PRIVATE "LINKER:--wrap=pthread_mutex_trylock")
add_test(NAME orbital_budget_check COMMAND orbital_budget_check)
set_tests_properties(orbital_budget_check PROPERTIES TIMEOUT 60)

set_tests_properties(orbital_allocation_check orbital_object_check orbital_representation_check orbital_finite_check
  orbital_reclamation_check orbital_window_check orbital_return_check
  orbital_worker_check orbital_lifetime_check orbital_thread_check
  orbital_preparation_check orbital_notification_check orbital_budget_check
  PROPERTIES LABELS "orbital-runtime")

add_custom_target(orbital_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
    --output-on-failure --no-tests=error -L "^orbital-runtime$"
  DEPENDS orbital_allocation_check orbital_object_check orbital_representation_check orbital_finite_check orbital_reclamation_check
    orbital_window_check orbital_return_check orbital_worker_check
    orbital_lifetime_check orbital_thread_check orbital_preparation_check orbital_notification_check orbital_budget_check
  USES_TERMINAL VERBATIM)
