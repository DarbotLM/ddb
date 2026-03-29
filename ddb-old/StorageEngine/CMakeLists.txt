add_library(DDB_health STATIC
  HealthData.cpp)
target_link_libraries(DDB_health arango)
target_include_directories(DDB_health PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_library(DDB_storage_engine STATIC
  EngineSelectorFeature.cpp
  HotBackup.cpp
  PhysicalCollection.cpp
  TransactionCollection.cpp
  TransactionState.cpp
  StorageEngine.cpp
  VPackSortMigration.cpp)
target_link_libraries(DDB_storage_engine
  DDB_cluster_engine
  DDB_cluster_methods
  DDB_rocksdb
  arango::validation
  DDB_replication2
  immer)
target_include_directories(DDB_storage_engine PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

# DDB_storage_engine uses DDB_rocksdb.
# In turn, DDB_storage_engine_common provides something for both.
add_library(DDB_storage_engine_common STATIC
  ReplicationIterator.cpp
  WalAccess.cpp)
target_link_libraries(DDB_storage_engine_common
  arango::validation
  DDB_replication2
  immer)
target_include_directories(DDB_storage_engine_common PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
