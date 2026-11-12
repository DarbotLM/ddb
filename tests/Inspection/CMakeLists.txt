add_library(ddb_inspection_test_helper INTERFACE)

target_include_directories(ddb_inspection_test_helper
  INTERFACE
  include)

add_library(ddb_tests_inspection OBJECT
  InspectionTest.cpp
  JsonPrintTest.cpp
  TransformerTest.cpp
  ValidateTest.cpp
  ValidateTest.cpp
  VPackLoadTest.cpp
  VPackSaveTest.cpp)

target_link_libraries(ddb_tests_inspection
  PRIVATE gtest
          velocypack
          ddb_lightweight # jezas.
          ddb_inspection
          ddb_inspection_test_helper)

add_executable(arangodbtests_inspection
  EXCLUDE_FROM_ALL)

target_link_libraries(arangodbtests_inspection
    ddb_tests_inspection
    ddb_crashhandler_light
    velocypack_utils
    gtest_main)

add_test(NAME inspection
         COMMAND arangodbtests_inspection)
