add_library(ddb_tests_build_id OBJECT
  BuildIdTest.cpp)

target_link_libraries(ddb_tests_build_id
  PUBLIC
    gtest
    ddb_build_id)

add_executable(arangodbtests_build_id
  EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_build_id
  gtest_main
  ddb_build_id
  ddb_tests_build_id)
target_link_options(arangodbtests_build_id
  PUBLIC
  -Wl,--build-id)
