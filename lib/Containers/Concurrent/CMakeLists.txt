add_library(ddb_thread_owning_list STATIC
  thread.cpp)
target_include_directories(ddb_thread_owning_list PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ddb_thread_owning_list
  PRIVATE
  ddb_basic_utils
  ddb_inspection)

add_library(ddb_shared INTERFACE
  shared.h)
# TODO need to have include dir and include ${CMAKE_CURRENT_SOURCE_DIR}/include
target_include_directories(ddb_shared INTERFACE ${PROJECT_SOURCE_DIR}/lib)
