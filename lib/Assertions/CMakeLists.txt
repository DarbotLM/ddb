add_library(ddb_assertions STATIC
  AssertionConditionalLogger.cpp
  AssertionLogger.cpp)

target_link_libraries(ddb_assertions
  PUBLIC
  ddb_crashhandler
)

target_include_directories(ddb_assertions
  PUBLIC
  ${PROJECT_SOURCE_DIR}/lib
  ${PROJECT_BINARY_DIR}/lib)

