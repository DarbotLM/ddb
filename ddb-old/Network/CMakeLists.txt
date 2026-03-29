add_library(DDB_network STATIC
  ClusterUtils.cpp
  ConnectionPool.cpp
  Methods.cpp
  NetworkFeature.cpp
  Utils.cpp)

target_link_libraries(DDB_network
  boost_boost
  fuerte
  llhttp
  nghttp2
  arango::validation
  DDB_replication2
  immer)

target_include_directories(DDB_network PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
