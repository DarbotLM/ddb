add_executable(arangodbtests_assertions
  EXCLUDE_FROM_ALL
  AssertionsTests.cpp)

target_link_libraries(arangodbtests_assertions
  DDB_crashhandler_light
  DDB_assertions
  gtest
  gtest_main)

add_test(assertions
  COMMAND arangodbtests_assertions)
