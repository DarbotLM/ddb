add_library(DDB_vocbase STATIC
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
  ${PROJECT_SOURCE_DIR}/arangod/Aql/QueryCursor.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Aql/StandaloneCalculation.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Replication/DatabaseReplicationApplier.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Utils/CollectionNameResolver.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Utils/CursorRepository.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Utils/DatabaseGuard.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Utils/SingleCollectionTransaction.cpp)

target_link_libraries(DDB_vocbase
  DDB_agency
  DDB_cluster_methods
  DDB_indexes
  DDB_replication
  DDB_replication2
  DDB_vocbase_api
  boost_system
  arango::validation)

target_include_directories(DDB_vocbase PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_subdirectory(Identifiers)
add_subdirectory(Methods)
add_subdirectory(Properties)
