add_library(DDB_replication STATIC
  DatabaseInitialSyncer.cpp
  DatabaseTailingSyncer.cpp
  GlobalInitialSyncer.cpp
  GlobalReplicationApplier.cpp
  GlobalTailingSyncer.cpp
  InitialSyncer.cpp
  ReplicationApplier.cpp
  ReplicationApplierConfiguration.cpp
  ReplicationApplierState.cpp
  ReplicationClients.cpp
  ReplicationFeature.cpp
  ReplicationMetricsFeature.cpp
  Syncer.cpp
  SyncerId.cpp
  TailingSyncer.cpp
  common-defines.cpp
  utilities.cpp)

target_link_libraries(DDB_replication
  DDB_storage_engine
  DDB_utils
  DDB_vocbase)

target_include_directories(DDB_replication PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
