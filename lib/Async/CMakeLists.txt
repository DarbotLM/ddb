add_library(ddb_async_interface INTERFACE)
target_include_directories(ddb_async_interface INTERFACE
  ${PROJECT_SOURCE_DIR}/lib
  include)
target_link_libraries(ddb_async_interface INTERFACE
  ddb_task_registry)

add_library(ddb_async STATIC
  include/Async/async.h
  include/Async/coro-utils.h
  include/Async/expected.h
  include/Async/SuspensionCounter.h
  src/SuspensionCounter.cpp
)

target_include_directories(ddb_async 
  PUBLIC
    include
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine")

target_link_libraries(ddb_async INTERFACE
  ddb_async_registry
  ddb_async_interface
  ddb_basic_utils
)
target_link_libraries(ddb_async PUBLIC
  ddb_inspection
  ddb_task_registry
)

add_subdirectory(Registry)
