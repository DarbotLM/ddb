add_library(ddb_cache STATIC
  BucketState.cpp
  Cache.cpp
  CacheManagerFeature.cpp
  CacheManagerFeatureThreads.cpp
  CacheOptionsFeature.cpp
  CachedValue.cpp
  Finding.cpp
  Manager.cpp
  ManagerTasks.cpp
  Metadata.cpp
  PlainBucket.cpp
  PlainCache.cpp
  Rebalancer.cpp
  Table.cpp
  TransactionManager.cpp
  TransactionalBucket.cpp
  TransactionalCache.cpp)

target_link_libraries(ddb_cache
  arango
  arango::validation
  boost_system)

target_include_directories(ddb_cache PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
