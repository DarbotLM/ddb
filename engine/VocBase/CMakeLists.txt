add_library(ddb_vocbase STATIC
  ComputedValues.cpp
  KeyGenerator.cpp
  LogicalCollection.cpp
  LogicalDataSource.cpp
  LogicalView.cpp
  Validators.cpp
  VocbaseInfo.cpp
  VocBaseLogManager.cpp
  ticks.cpp
  vocbase.cpp
  VocbaseMetrics.cpp
  ${PROJECT_SOURCE_DIR}/engine/Aql/QueryCursor.cpp
  ${PROJECT_SOURCE_DIR}/engine/Aql/StandaloneCalculation.cpp
  ${PROJECT_SOURCE_DIR}/engine/Replication/DatabaseReplicationApplier.cpp
  ${PROJECT_SOURCE_DIR}/engine/Utils/CollectionNameResolver.cpp
  ${PROJECT_SOURCE_DIR}/engine/Utils/CursorRepository.cpp
  ${PROJECT_SOURCE_DIR}/engine/Utils/DatabaseGuard.cpp
  ${PROJECT_SOURCE_DIR}/engine/Utils/SingleCollectionTransaction.cpp)

target_link_libraries(ddb_vocbase
  ddb_agency
  ddb_cluster_methods
  ddb_indexes
  ddb_replication
  ddb_replication2
  ddb_vocbase_api
  boost_system
  arango::validation)

target_include_directories(ddb_vocbase PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_subdirectory(Identifiers)
add_subdirectory(Methods)
add_subdirectory(Properties)
