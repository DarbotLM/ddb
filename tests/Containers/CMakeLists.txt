add_library(ddb_tests_forest OBJECT
  ForestTest.cpp)
target_link_libraries(ddb_tests_forest PRIVATE
  ddb_forest
  gtest)

add_executable(arangodbtests_forest EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_forest
  ddb_tests_forest
  gtest_main)

add_subdirectory(Concurrent)
