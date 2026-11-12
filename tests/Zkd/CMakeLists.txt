add_library(ddb_tests_zkd OBJECT
  Conversion.cpp
  Library.cpp)
target_link_libraries(ddb_tests_zkd
  PRIVATE
    ddb_rocksdb
    gtest)	
target_include_directories(ddb_tests_zkd
  PRIVATE
    ${PROJECT_SOURCE_DIR}/engine)

if (USE_FAST_DOUBLE_MEMCMP_ENCODING)
    target_compile_definitions(ddb_tests_zkd PRIVATE USE_FAST_DOUBLE_MEMCMP_ENCODING)
endif ()

add_executable(darbotdbtests_zkd EXCLUDE_FROM_ALL)
target_link_libraries(darbotdbtests_zkd
  ddb_tests_zkd
  gtest_main)

add_test(NAME zkd
         COMMAND darbotdbtests_zkd)
