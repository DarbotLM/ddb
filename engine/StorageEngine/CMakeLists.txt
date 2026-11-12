add_library(ddb_health STATIC
  HealthData.cpp)
target_link_libraries(ddb_health arango)
target_include_directories(ddb_health PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_library(ddb_storage_engine STATIC
  EngineSelectorFeature.cpp
  HotBackup.cpp
  PhysicalCollection.cpp
  TransactionCollection.cpp
  TransactionState.cpp
  StorageEngine.cpp
  VPackSortMigration.cpp)
target_link_libraries(ddb_storage_engine
  ddb_cluster_engine
  ddb_cluster_methods
  ddb_rocksdb
  arango::validation
  ddb_replication2
  immer)
target_include_directories(ddb_storage_engine PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

# ddb_storage_engine uses ddb_rocksdb.
# In turn, ddb_storage_engine_common provides something for both.
add_library(ddb_storage_engine_common STATIC
  ReplicationIterator.cpp
  WalAccess.cpp)
target_link_libraries(ddb_storage_engine_common
  arango::validation
  ddb_replication2
  immer)
target_include_directories(ddb_storage_engine_common PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
