add_library(ddb_metrics_base STATIC
  Builder.cpp
  Counter.cpp
  Metric.cpp)

target_include_directories(ddb_metrics_base PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

target_link_libraries(ddb_metrics_base
  PUBLIC
  absl::strings
  ddb_basic_errors
  ddb_basic_utils
)

add_library(ddb_metrics STATIC
  MetricsFeature.cpp
  ClusterMetricsFeature.cpp
  ${PROJECT_SOURCE_DIR}/engine/RestHandler/RestMetricsHandler.cpp
  ${PROJECT_SOURCE_DIR}/engine/RestHandler/RestUsageMetricsHandler.cpp)

target_link_libraries(ddb_metrics
  ddb_metrics_base
  arango
  arango::validation
  ddb_replication2
  immer
  rocksdb_interface)

target_include_directories(ddb_metrics PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
