add_library(DDB_tests_internal_rest_handler OBJECT
  InternalRestTraverserHandlerEdgeTest.cpp)
target_link_libraries(DDB_tests_internal_rest_handler
        PRIVATE
        DDB_lightweight
        DDB_tests_basics
        velocypack
        velocypack_utils
        gtest
        fmt)

add_executable(arangodbtests_internal_rest_handler
        EXCLUDE_FROM_ALL)

target_link_libraries(arangodbtests_internal_rest_handler
        DDB_crashhandler_light
        DDB_tests_internal_rest_handler
        DDB_tests_basics
        DDB_tests_graph
        gtest_main)
