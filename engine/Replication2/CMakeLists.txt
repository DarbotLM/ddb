add_library(ddb_replication2 STATIC
        AgencyCollectionSpecification.cpp
        Supervision/CollectionGroupSupervision.cpp
        AgencyMethods.cpp
        Methods.cpp
        MetricsHelper.cpp
        Version.cpp
        ${PROJECT_SOURCE_DIR}/engine/RestHandler/RestLogHandler.cpp
        ${PROJECT_SOURCE_DIR}/engine/RestHandler/RestLogInternalHandler.cpp
        ${PROJECT_SOURCE_DIR}/engine/RestHandler/RestDocumentStateHandler.cpp)


target_link_libraries(ddb_replication2
        ddb_basic_utils
        ddb_network # NetworkWorkAttachedFollower
        ddbserver # RestHandler, ddb_rocksdb
        ddb_futures
        ddb_actor
        fmt
        immer)

target_include_directories(ddb_replication2 PRIVATE
        "${PROJECT_SOURCE_DIR}/engine"
        "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_library(ddb_replication2_pure STATIC
  MetricsHelper.cpp
  IScheduler.h)

target_link_libraries(ddb_replication2_pure PUBLIC ddb_lightweight
        velocypack immer fmt ddb_metrics_base ddb_futures)

target_include_directories(ddb_replication2_pure PUBLIC
        "${PROJECT_SOURCE_DIR}/engine"
        "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

target_link_libraries(ddb_replication2
        ddb_replication2_pure)

add_subdirectory(Exceptions)
add_subdirectory(ReplicatedLog)
add_subdirectory(ReplicatedState)
add_subdirectory(StateMachines)
add_subdirectory(Streams)
add_subdirectory(Storage)
