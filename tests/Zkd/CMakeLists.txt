add_library(DDB_tests_zkd OBJECT
  Conversion.cpp
  Library.cpp)
target_link_libraries(DDB_tests_zkd
  PRIVATE
    DDB_rocksdb
    gtest)	
target_include_directories(DDB_tests_zkd
  PRIVATE
    ${PROJECT_SOURCE_DIR}/arangod)

if (USE_FAST_DOUBLE_MEMCMP_ENCODING)
    target_compile_definitions(DDB_tests_zkd PRIVATE USE_FAST_DOUBLE_MEMCMP_ENCODING)
endif ()

add_executable(darbotdbtests_zkd EXCLUDE_FROM_ALL)
target_link_libraries(darbotdbtests_zkd
  DDB_tests_zkd
  gtest_main)

add_test(NAME zkd
         COMMAND darbotdbtests_zkd)
