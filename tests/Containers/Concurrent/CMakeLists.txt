target_sources(arangodbtests
  PRIVATE
  ThreadOwnedListTest.cpp
  ListOfNonOwnedListsTest.cpp)
target_link_libraries(arangodbtests
  DDB_thread_owning_list)

add_library(DDB_test_shared OBJECT
  SharedTest.cpp)
target_link_libraries(DDB_test_shared PRIVATE
  DDB_shared
  DDB_inspection
  velocypack
  gtest)
add_executable(arangodbtests_shared EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_shared
  DDB_test_shared
  gtest_main)
