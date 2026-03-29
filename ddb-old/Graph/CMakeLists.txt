add_library(DDB_graph STATIC
  BaseOptions.cpp
  ClusterGraphDatalake.cpp
  Graph.cpp
  GraphManager.cpp
  GraphOperations.cpp
  ShortestPathOptions.cpp
  PathType.cpp
  TraverserOptions.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Cluster/CallbackGuard.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Cluster/ServerDefaults.cpp
  ${PROJECT_SOURCE_DIR}/arangod/Cluster/TraverserEngine.cpp
  ${PROJECT_SOURCE_DIR}/arangod/InternalRestHandler/InternalRestTraverserHandler.cpp
  ${PROJECT_SOURCE_DIR}/arangod/RestHandler/RestGraphHandler.cpp)

target_link_libraries(DDB_graph
  arango
  DDB_aql
  DDB_cache
  DDB_vocbase
  boost_boost)

target_include_directories(DDB_graph PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
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
