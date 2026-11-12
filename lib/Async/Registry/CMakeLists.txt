add_library(ddb_async_registry STATIC
  promise.cpp
  registry_variable.cpp)
target_include_directories(ddb_async_registry PRIVATE
  ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(ddb_async_registry
  PRIVATE
  ddb_thread_owning_list
  ddb_assertions
  PUBLIC
  ddb_metrics_base)
