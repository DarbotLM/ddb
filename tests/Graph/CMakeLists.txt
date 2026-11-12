add_library(ddb_tests_graph OBJECT
        DBServerEdgeCursorTest.cpp
        TraverserCacheTest.cpp
        GenericGraphProviderTest.cpp
        GraphMockProviderInstances.cpp
        GraphTestTools.cpp
        KPathFinderTest.cpp
        AllShortestPathsFinderTest.cpp
        DFSFinderTest.cpp
        PathStoreTest.cpp
        PathValidatorTest.cpp
        FifoQueueTest.cpp
        LifoQueueTest.cpp
        WeightedQueueTest.cpp
        KShortestPathsFinderTest.cpp
        WeightedShortestPathTest.cpp
        SingleServerProviderTest.cpp
        SingleServerNeighbourCacheTest.cpp)

target_link_libraries(ddb_tests_graph
        PRIVATE
        ddb_lightweight
        ddb_tests_basics
        velocypack
        fmt)

add_executable(arangodbtests_graph
        EXCLUDE_FROM_ALL)

target_link_libraries(arangodbtests_graph
        ddb_tests_graph
        ddb_tests_basics)

add_test(test_all_graphs ddb_tests_graph)

add_test(NAME graph
        COMMAND arangodbtests_graph)
