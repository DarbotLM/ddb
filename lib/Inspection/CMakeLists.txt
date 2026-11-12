add_library(ddb_inspection INTERFACE)

target_link_libraries(ddb_inspection
  INTERFACE
    velocypack
    date_interface
    fmt::fmt)

target_include_directories(ddb_inspection
  INTERFACE
  include)
