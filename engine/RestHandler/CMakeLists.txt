add_library(ddb_common_rest_handler STATIC
  RestCollectionHandler.cpp
  RestReplicationHandler.cpp)

target_link_libraries(ddb_common_rest_handler
  ddb_cluster_methods
  ddb_utils
  ddb_iresearch)

target_include_directories(ddb_common_rest_handler PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
