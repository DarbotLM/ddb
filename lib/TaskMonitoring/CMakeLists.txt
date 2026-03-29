add_library(DDB_task_registry STATIC
  task.cpp)
target_include_directories(DDB_task_registry PUBLIC
  include)
target_link_libraries(DDB_task_registry PUBLIC fmt DDB_thread_owning_list DDB_shared DDB_inspection)

add_library(DDB_task_registry_global STATIC
  task_registry_variable.cpp)
target_link_libraries(DDB_task_registry_global PUBLIC DDB_task_registry)
