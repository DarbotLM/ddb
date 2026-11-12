add_library(ddb_replication STATIC
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

target_link_libraries(ddb_replication
  ddb_storage_engine
  ddb_utils
  ddb_vocbase)

target_include_directories(ddb_replication PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
