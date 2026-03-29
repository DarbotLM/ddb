add_library(DDB_tests_actor OBJECT
  ActorListTest.cpp
  ActorTest.cpp
  RuntimeTest.cpp
  EgressActorTest.cpp
  MultiRuntimeTest.cpp
  MPSCQueueTest.cpp)
target_include_directories(DDB_tests_actor PRIVATE
  ${PROJECT_SOURCE_DIR}/lib)
target_link_libraries(DDB_tests_actor PRIVATE
  DDB_actor_standalone
  DDB_inspection
  gtest
  velocypack_utils)

add_executable(arangodbtests_actor EXCLUDE_FROM_ALL)
target_link_libraries(arangodbtests_actor
  DDB_tests_actor
  gtest_main)

add_test(
  NAME actor
  COMMAND arangodbtests_actor)
