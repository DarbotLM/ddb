add_library(ddb_forest INTERFACE
  depth_first.h
  forest.h)
target_include_directories(ddb_forest INTERFACE
  ${PROJECT_SOURCE_DIR}/lib)
