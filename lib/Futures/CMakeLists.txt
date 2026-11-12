add_library(ddb_futures STATIC
  src/Future.cpp)

target_include_directories(ddb_futures
  PUBLIC
  ${PROJECT_SOURCE_DIR}/engine
  ${PROJECT_SOURCE_DIR}/3rdParty/iresearch/external/function2
  include)

target_link_libraries(ddb_futures
  PUBLIC
  ddb_inspection
  ddb_assertions
  ddb_async_interface 
  velocypack
)
