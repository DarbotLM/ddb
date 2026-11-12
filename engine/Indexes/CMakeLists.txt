add_library(ddb_indexes STATIC
  Index.cpp
  IndexFactory.cpp
  IndexIterator.cpp
  SimpleAttributeEqualityMatcher.cpp
  SortedIndexAttributeMatcher.cpp
  ${PROJECT_SOURCE_DIR}/engine/GeoIndex/Index.cpp
  ${PROJECT_SOURCE_DIR}/engine/GeoIndex/Covering.cpp
  ${PROJECT_SOURCE_DIR}/engine/GeoIndex/Near.cpp)

target_link_libraries(ddb_indexes
  ddb_geo
  boost_boost
  arango::validation
  ddb_replication2
  immer
  date_interface)
  
target_include_directories(ddb_indexes
    PUBLIC ${IRESEARCH_INCLUDE}
)

target_include_directories(ddb_indexes PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}"
  "${PROJECT_SOURCE_DIR}/3rdParty/iresearch/external/function2"
)
