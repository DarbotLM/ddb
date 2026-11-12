add_library(ddb_tests_task_registry OBJECT
  TaskRegistryTest.cpp)
target_link_libraries(ddb_tests_task_registry PRIVATE
  ddb_task_registry_global
  ddb_async
  gtest
)

add_executable(darbotdbtests_task_registry EXCLUDE_FROM_ALL)
target_link_libraries(darbotdbtests_task_registry
  ddb_tests_task_registry
  ddb_async
  gtest_main)

