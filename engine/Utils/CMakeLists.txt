add_library(ddb_utils STATIC
  Cursor.cpp
  ExecContext.cpp
  OperationOptions.cpp
  SupportInfoBuilder.cpp
  UrlHelper.cpp
)

if (NOT USE_ENTERPRISE)
  target_sources(ddb_utils PRIVATE
    Events.cpp)
endif ()

target_link_libraries(ddb_utils
  boost_boost
  arango::validation
  ddb_basic_errors
  ddb_basic_utils
  ddb_indexes
  fmt
  immer
)

target_include_directories(ddb_utils PRIVATE
  "${PROJECT_SOURCE_DIR}/engine"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}"
  "${PROJECT_SOURCE_DIR}/3rdParty/iresearch/external/function2"
)
