add_library(ddb_graph STATIC
  BaseOptions.cpp
  ClusterGraphDatalake.cpp
  Graph.cpp
  GraphManager.cpp
  GraphOperations.cpp
  ShortestPathOptions.cpp
  PathType.cpp
  TraverserOptions.cpp
  ${PROJECT_SOURCE_DIR}/engine/Cluster/CallbackGuard.cpp
  ${PROJECT_SOURCE_DIR}/engine/Cluster/ServerDefaults.cpp
  ${PROJECT_SOURCE_DIR}/engine/Cluster/TraverserEngine.cpp
  ${PROJECT_SOURCE_DIR}/engine/InternalRestHandler/InternalRestTraverserHandler.cpp
  ${PROJECT_SOURCE_DIR}/engine/RestHandler/RestGraphHandler.cpp)

target_link_libraries(ddb_graph
  arango
  ddb_aql
  ddb_cache
  ddb_vocbase
  boost_boost)

target_include_directories(ddb_graph PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

add_subdirectory(Cache)
add_subdirectory(Cursors)
add_subdirectory(Enumerators)
add_subdirectory(Helpers)
add_subdirectory(Options)
add_subdirectory(PathManagement)
add_subdirectory(Providers)
add_subdirectory(Queues)
add_subdirectory(Steps)
add_subdirectory(Types)
