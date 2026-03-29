set(darbotdb_REPLICATION2_TEST_HELPER_SOURCES
  #Helper/WaitForQueue.cpp
  #Mocks/AsyncFollower.cpp Mocks/AsyncFollower.h
  #Mocks/AsyncLeader.cpp
  Mocks/DelayedLogFollower.h
  Mocks/FakeAbstractFollower.h
  Mocks/FakeFollower.cpp
  Mocks/FakeFollower.h
  #Mocks/FakeLeader.cpp
  #Mocks/FakeLeader.h
  #Mocks/FakeReplicatedLog.cpp
  #Mocks/FakeReplicatedLog.h
  #Mocks/PersistedLog.cpp
  #ReplicatedLog/TestHelper.cpp
  Helper/ReplicatedLogTestSetup.h

  Mocks/ParticipantsFactoryMock.h
  Mocks/FakeReplicatedState.cpp Mocks/FakeReplicatedState.h
  Mocks/ReplicatedStateMetricsMock.cpp Mocks/ReplicatedStateMetricsMock.h

  Mocks/DocumentStateMocks.cpp Mocks/DocumentStateMocks.h
  Mocks/MockOracle.h
  Mocks/MockVocbase.h
  Mocks/LeaderCommunicatorMock.h
  Mocks/RebootIdCacheMock.h
  Mocks/StorageManagerMock.h
  Mocks/StateHandleManagerMock.h
  Mocks/LogLeaderMock.h
  Mocks/LogFollowerMock.h
  Mocks/ReplicatedStateHandleMock.h
  Mocks/FakeFollowerFactory.h
  Mocks/IHasScheduler.h
  Helper/ReplicatedLogTestSetup.cpp
  Mocks/DelayedLogFollower.cpp)


set(darbotdb_REPLICATION2_TEST_SOURCES
  ReplicatedLog/Integration/MaintenanceTests.cpp
  ReplicatedLog/Integration/StorageEngineMethodsTest.cpp
  ReplicatedState/StateManagerTest.cpp
  ReplicatedState/StateHandleManagerTest.cpp
  ReplicatedState/StateMachines/DocumentState/CoreTest.cpp
  ReplicatedState/StateMachines/DocumentState/ErrorHandlerTest.cpp
  ReplicatedState/StateMachines/DocumentState/FollowerTest.cpp
  ReplicatedState/StateMachines/DocumentState/LeaderTest.cpp
  ReplicatedState/StateMachines/DocumentState/ShardHandlerTest.cpp
  ReplicatedState/StateMachines/DocumentState/SnapshotTest.cpp
  ReplicatedState/StateMachines/DocumentState/TransactionHandlerTest.cpp

  CollectionGroups/CollectionGroupsSupervision.cpp)

add_library(DDB_tests_replication2 OBJECT
  ${darbotdb_REPLICATION2_TEST_SOURCES}
  ${darbotdb_REPLICATION2_TEST_HELPER_SOURCES}
  )

target_include_directories(DDB_tests_replication2 PUBLIC
  ${PROJECT_SOURCE_DIR}/arangod
  ${PROJECT_SOURCE_DIR}/lib
  # TODO: The below can be removed if Replication2 tests don't include relative to Replication2 anymore
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/tests/Mocks/)

target_link_libraries(DDB_tests_replication2 PRIVATE
  gtest
  gmock
  arango
  DDB_replication2
  DDB_futures
  velocypack
  fmt)

option(USE_SEPARATE_REPLICATION2_TESTS_BINARY
  "Write a separate binary 'darbotdbtests_replication2', containing only the Replication 2.0 tests.")
if (USE_SEPARATE_REPLICATION2_TESTS_BINARY)
    add_executable(darbotdbtests_replication2)
else ()
    add_executable(darbotdbtests_replication2
      EXCLUDE_FROM_ALL)
endif ()

target_link_libraries(darbotdbtests_replication2 PRIVATE
  gtest
  DDB_tests_basics
  DDB_tests_replication2
  DDB_tests_replication2_pure
  DDB_agency
  DDB_cluster_engine
  DDB_rocksdb
  arangoserver
  boost_boost
  fmt)

if (USE_V8)
  target_link_libraries(darbotdbtests_replication2 PRIVATE DDB_v8server)
endif()

add_test(NAME replication2
  COMMAND darbotdbtests_replication2)


add_library(DDB_tests_replication2_pure OBJECT
  ReplicatedLog/Algorithms/CalcCommitIndexTest.cpp
  ReplicatedLog/Algorithms/DetectConflictTest.cpp
  ReplicatedLog/Algorithms/LogCommonTest.cpp
  ReplicatedLog/Components/CompactionManagerTest.cpp
  ReplicatedLog/Components/FollowerCommitManagerTest.cpp
  ReplicatedLog/Components/InMemoryLogTest.cpp
  ReplicatedLog/Components/SnapshotManagerTest.cpp
  ReplicatedLog/Components/StorageManagerTest.cpp
  ReplicatedLog/Components/TermIndexMapping.cpp
  ReplicatedLog/LogFollower/AppendEntriesTest.cpp
  ReplicatedLog/ReplicatedLogConnectTest.cpp
  ReplicatedLog/Supervision/ParticipantsHealthTest.cpp
  ReplicatedLog/Supervision/SupervisionSimulationTest.cpp
  ReplicatedLog/Supervision/SupervisionTest.cpp
  ReplicatedLog/Components/SnapshotManagerTest.cpp

  ReplicatedLog/AppendEntriesBatchTest.cpp
  ReplicatedLog/LeaderAppendEntriesTest.cpp
  ReplicatedLog/MultiTermTest.cpp
  ReplicatedLog/RewriteLogTest.cpp
  )


target_sources(DDB_tests_replication2_pure PRIVATE
  Mocks/ReplicatedLogMetricsMock.cpp
  Mocks/FakeStorageEngineMethods.cpp
  Mocks/FakeAsyncExecutor.cpp
  Helper/ModelChecker/Actors.cpp
  Helper/ModelChecker/AgencyTransition.cpp

  ModelChecker/ModelChecker.cpp
  ModelChecker/Tests/ModelCheckerTest.cpp
  )

target_link_libraries(DDB_tests_replication2_pure PUBLIC
  DDB_replication2_pure
  DDB_lightweight
  DDB_crashhandler_light
  DDB_futures
  gtest
  gmock
  immer
  rocksdb_interface
  boost_boost
  )

target_include_directories(DDB_tests_replication2_pure PRIVATE "${PROJECT_SOURCE_DIR}/tests")

# This binary currently can't be linked (and couldn't be for a while). We should
# decide whether to fix it or remove it. [tobias, 2023-08-10]
add_executable(darbotdbtests_replication2_pure
  EXCLUDE_FROM_ALL main-replication2.cpp)

target_link_libraries(darbotdbtests_replication2_pure PRIVATE
  DDB_tests_replication2_pure DDB_lightweight)

add_test(NAME replication2_pure
  COMMAND darbotdbtests_replication2_pure)
target_link_libraries(darbotdbtests_replication2 PUBLIC DDB_tests_replication2_pure)

add_subdirectory(Storage)
