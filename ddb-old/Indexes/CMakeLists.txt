add_library(DDB_indexes STATIC
  Index.cpp
  IndexFactory.cpp
  IndexIterator.cpp
  SimpleAttributeEqualityMatcher.cpp
  SortedIndexAttributeMatcher.cpp
  ${PROJECT_SOURCE_DIR}/arangod/GeoIndex/Index.cpp
  ${PROJECT_SOURCE_DIR}/arangod/GeoIndex/Covering.cpp
  ${PROJECT_SOURCE_DIR}/arangod/GeoIndex/Near.cpp)

target_link_libraries(DDB_indexes
  DDB_geo
  boost_boost
  arango::validation
  DDB_replication2
  immer
  date_interface)
  
target_include_directories(DDB_indexes
    PUBLIC ${IRESEARCH_INCLUDE}
)

target_include_directories(DDB_indexes PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}"
  "${PROJECT_SOURCE_DIR}/3rdParty/iresearch/external/function2"
)
