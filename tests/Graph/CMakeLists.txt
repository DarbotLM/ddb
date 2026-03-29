add_library(DDB_tests_graph OBJECT
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

target_link_libraries(DDB_tests_graph
        PRIVATE
        DDB_lightweight
        DDB_tests_basics
        velocypack
        fmt)

add_executable(arangodbtests_graph
        EXCLUDE_FROM_ALL)

target_link_libraries(arangodbtests_graph
        DDB_tests_graph
        DDB_tests_basics)

add_test(test_all_graphs DDB_tests_graph)

add_test(NAME graph
        COMMAND arangodbtests_graph)
