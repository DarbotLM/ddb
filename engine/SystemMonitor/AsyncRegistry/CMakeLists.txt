target_sources(ddbserver PRIVATE
  Feature.cpp
  Metrics.cpp
  RestHandler.cpp)
target_link_libraries(ddbserver
  ddb_forest)
