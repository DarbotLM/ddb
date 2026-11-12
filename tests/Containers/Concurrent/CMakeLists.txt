target_sources(arangodbtests
  PRIVATE
  ThreadOwnedListTest.cpp
  ListOfNonOwnedListsTest.cpp)
target_link_libraries(arangodbtests
  ddb_thread_owning_list)

add_library(ddb_test_shared OBJECT
  SharedTest.cpp)
target_link_libraries(ddb_test_shared PRIVATE
  ddb_shared
  ddb_inspection
  velocypack
  gtest)
add_executable(arangodbtests_shared EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_shared
  ddb_test_shared
  gtest_main)
