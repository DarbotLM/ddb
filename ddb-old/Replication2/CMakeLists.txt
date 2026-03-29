add_library(DDB_replication2 STATIC
        AgencyCollectionSpecification.cpp
        Supervision/CollectionGroupSupervision.cpp
        AgencyMethods.cpp
        Methods.cpp
        MetricsHelper.cpp
        Version.cpp
        ${PROJECT_SOURCE_DIR}/arangod/RestHandler/RestLogHandler.cpp
        ${PROJECT_SOURCE_DIR}/arangod/RestHandler/RestLogInternalHandler.cpp
        ${PROJECT_SOURCE_DIR}/arangod/RestHandler/RestDocumentStateHandler.cpp)


target_link_libraries(DDB_replication2
        DDB_basic_utils
        DDB_network # NetworkWorkAttachedFollower
        arangoserver # RestHandler, DDB_rocksdb
        DDB_futures
        DDB_actor
        fmt
        immer)

target_include_directories(DDB_replication2 PRIVATE
        "${PROJECT_SOURCE_DIR}/arangod"
        "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_library(DDB_replication2_pure STATIC
  MetricsHelper.cpp
  IScheduler.h)

target_link_libraries(DDB_replication2_pure PUBLIC DDB_lightweight
        velocypack immer fmt DDB_metrics_base DDB_futures)

target_include_directories(DDB_replication2_pure PUBLIC
        "${PROJECT_SOURCE_DIR}/arangod"
        "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

target_link_libraries(DDB_replication2
        DDB_replication2_pure)

add_subdirectory(Exceptions)
add_subdirectory(ReplicatedLog)
add_subdirectory(ReplicatedState)
add_subdirectory(StateMachines)
add_subdirectory(Streams)
add_subdirectory(Storage)
