add_library(DDB_tests_task_registry OBJECT
  TaskRegistryTest.cpp)
target_link_libraries(DDB_tests_task_registry PRIVATE
  DDB_task_registry_global
  DDB_async
  gtest
)

add_executable(darbotdbtests_task_registry EXCLUDE_FROM_ALL)
target_link_libraries(darbotdbtests_task_registry
  DDB_tests_task_registry
  DDB_async
  gtest_main)

