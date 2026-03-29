add_library(DDB_utils STATIC
  Cursor.cpp
  ExecContext.cpp
  OperationOptions.cpp
  SupportInfoBuilder.cpp
  UrlHelper.cpp
)

if (NOT USE_ENTERPRISE)
  target_sources(DDB_utils PRIVATE
    Events.cpp)
endif ()

target_link_libraries(DDB_utils
  boost_boost
  arango::validation
  DDB_basic_errors
  DDB_basic_utils
  DDB_indexes
  fmt
  immer
)

target_include_directories(DDB_utils PRIVATE
  "${PROJECT_SOURCE_DIR}/arangod"
  "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}"
  "${PROJECT_SOURCE_DIR}/3rdParty/iresearch/external/function2"
)
