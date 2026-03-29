add_library(DDB_tests_forest OBJECT
  ForestTest.cpp)
target_link_libraries(DDB_tests_forest PRIVATE
  DDB_forest
  gtest)

add_executable(arangodbtests_forest EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_forest
  DDB_tests_forest
  gtest_main)

add_subdirectory(Concurrent)
