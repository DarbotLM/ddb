add_library(clienttools_utils STATIC
  ClientManager.cpp
  ManagedDirectory.cpp
)

target_link_libraries(clienttools_utils
  ${SYSTEM_LIBRARIES}
  ddb_basic_errors
  ddb_static_strings
  ddb_basic_utils
  ddb_basic_features
  ddb_basic_logger
  ddb_basic_http_client
  ddb_scheduler
  boost_system
  boost_boost
  fuerte
  v8_interface
)
target_include_directories(clienttools_utils PRIVATE ${CMAKE_SOURCE_DIR}/client-tools)

add_dependencies(clienttools_utils v8_build)

