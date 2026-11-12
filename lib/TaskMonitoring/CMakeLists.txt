add_library(ddb_task_registry STATIC
  task.cpp)
target_include_directories(ddb_task_registry PUBLIC
  include)
target_link_libraries(ddb_task_registry PUBLIC fmt ddb_thread_owning_list ddb_shared ddb_inspection)

add_library(ddb_task_registry_global STATIC
  task_registry_variable.cpp)
target_link_libraries(ddb_task_registry_global PUBLIC ddb_task_registry)
