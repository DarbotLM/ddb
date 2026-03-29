add_library(DDB_tests_build_id OBJECT
  BuildIdTest.cpp)

target_link_libraries(DDB_tests_build_id
  PUBLIC
    gtest
    DDB_build_id)

add_executable(arangodbtests_build_id
  EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_build_id
  gtest_main
  DDB_build_id
  DDB_tests_build_id)
target_link_options(arangodbtests_build_id
  PUBLIC
  -Wl,--build-id)
