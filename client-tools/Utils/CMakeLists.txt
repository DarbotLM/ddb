add_library(clienttools_utils STATIC
  ClientManager.cpp
  ManagedDirectory.cpp
)

target_link_libraries(clienttools_utils
  ${SYSTEM_LIBRARIES}
  DDB_basic_errors
  DDB_static_strings
  DDB_basic_utils
  DDB_basic_features
  DDB_basic_logger
  DDB_basic_http_client
  DDB_scheduler
  boost_system
  boost_boost
  fuerte
  v8_interface
)
target_include_directories(clienttools_utils PRIVATE ${CMAKE_SOURCE_DIR}/client-tools)

add_dependencies(clienttools_utils v8_build)

