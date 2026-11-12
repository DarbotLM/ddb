if (USE_V8)
  add_library(ddb_v8server STATIC
    FoxxFeature.cpp
    GlobalExecutorMethods.cpp
    V8Executor.cpp
    V8DealerFeature.cpp
    v8-actions.cpp
    v8-analyzers.cpp
    v8-collection-util.cpp
    v8-collection.cpp
    v8-dispatcher.cpp
    v8-general-graph.cpp
    v8-query.cpp
    v8-replicated-logs.cpp
    v8-replication.cpp
    v8-statistics.cpp
    v8-ttl.cpp
    v8-user-functions.cpp
    v8-user-structures.cpp
    v8-users.cpp
    v8-util.cpp
    v8-views.cpp
    v8-vocbase.cpp
    v8-voccursor.cpp
    v8-vocindex.cpp
    ${PROJECT_SOURCE_DIR}/engine/Cluster/v8-cluster.cpp
    ${PROJECT_SOURCE_DIR}/engine/Transaction/V8Context.cpp)
  
  target_link_libraries(ddb_v8server
    ddb_agency
    ddb_iresearch
    ddb_replication
    ddb_v8
    ddb_v8_no_deadline
    ${V8_LIBS})
  
  target_include_directories(ddb_v8server PRIVATE
    "${PROJECT_SOURCE_DIR}/engine"
    "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
endif()
