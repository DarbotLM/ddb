add_library(ddb_tests_futures OBJECT
  FutureTest.cpp
  FutureCoroutineTest.cpp
  PromiseTest.cpp
  TryTest.cpp)
target_link_libraries(ddb_tests_futures
  PRIVATE
    arango
    ddb_futures
    gtest
    velocypack
    fmt)

add_executable(arangodbtests_futures EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_futures
  ddb_crashhandler_light
  gtest_main
  ddb_tests_futures)

add_test(NAME futures
         COMMAND arangodbtests_futures)
