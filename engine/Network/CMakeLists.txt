add_library(ddb_network STATIC
  ClusterUtils.cpp
  ConnectionPool.cpp
  Methods.cpp
  NetworkFeature.cpp
  Utils.cpp)

target_link_libraries(ddb_network
  boost_boost
  fuerte
  llhttp
  nghttp2
  arango::validation
  ddb_replication2
  immer)

target_include_directories(ddb_network PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
