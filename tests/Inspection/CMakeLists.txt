add_library(DDB_inspection_test_helper INTERFACE)

target_include_directories(DDB_inspection_test_helper
  INTERFACE
  include)

add_library(DDB_tests_inspection OBJECT
  InspectionTest.cpp
  JsonPrintTest.cpp
  TransformerTest.cpp
  ValidateTest.cpp
  ValidateTest.cpp
  VPackLoadTest.cpp
  VPackSaveTest.cpp)

target_link_libraries(DDB_tests_inspection
  PRIVATE gtest
          velocypack
          DDB_lightweight # jezas.
          DDB_inspection
          DDB_inspection_test_helper)

add_executable(arangodbtests_inspection
  EXCLUDE_FROM_ALL)

target_link_libraries(arangodbtests_inspection
    DDB_tests_inspection
    DDB_crashhandler_light
    velocypack_utils
    gtest_main)

add_test(NAME inspection
         COMMAND arangodbtests_inspection)
