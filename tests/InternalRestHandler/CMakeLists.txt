add_library(ddb_tests_internal_rest_handler OBJECT
  InternalRestTraverserHandlerEdgeTest.cpp)
target_link_libraries(ddb_tests_internal_rest_handler
        PRIVATE
        ddb_lightweight
        ddb_tests_basics
        velocypack
        velocypack_utils
        gtest
        fmt)

add_executable(arangodbtests_internal_rest_handler
        EXCLUDE_FROM_ALL)

target_link_libraries(arangodbtests_internal_rest_handler
        ddb_crashhandler_light
        ddb_tests_internal_rest_handler
        ddb_tests_basics
        ddb_tests_graph
        gtest_main)
