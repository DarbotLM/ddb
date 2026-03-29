add_library(DDB_metrics_base STATIC
  Builder.cpp
  Counter.cpp
  Metric.cpp)

target_include_directories(DDB_metrics_base PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

target_link_libraries(DDB_metrics_base
  PUBLIC
  absl::strings
  DDB_basic_errors
  DDB_basic_utils
)

add_library(DDB_metrics STATIC
  MetricsFeature.cpp
  ClusterMetricsFeature.cpp
  ${PROJECT_SOURCE_DIR}/arangod/RestHandler/RestMetricsHandler.cpp
  ${PROJECT_SOURCE_DIR}/arangod/RestHandler/RestUsageMetricsHandler.cpp)

target_link_libraries(DDB_metrics
  DDB_metrics_base
  arango
  arango::validation
  DDB_replication2
  immer
  rocksdb_interface)

target_include_directories(DDB_metrics PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
