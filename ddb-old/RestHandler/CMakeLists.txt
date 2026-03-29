add_library(DDB_common_rest_handler STATIC
  RestCollectionHandler.cpp
  RestReplicationHandler.cpp)

target_link_libraries(DDB_common_rest_handler
  DDB_cluster_methods
  DDB_utils
  DDB_iresearch)

target_include_directories(DDB_common_rest_handler PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
