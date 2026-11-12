add_library(ddb_cluster_methods STATIC
  ClusterMethods.cpp
  ClusterCollectionMethods.cpp
  ClusterIndexMethods.cpp
  ClusterTtlMethods.cpp)

target_link_libraries(ddb_cluster_methods
  arango
  ddb_network
  zlib_interface)

target_include_directories(ddb_cluster_methods PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")

target_compile_options(ddb_cluster_methods PRIVATE
	${CMAKE_DEPRECATE_OPTIONS})

add_subdirectory(Utils)
