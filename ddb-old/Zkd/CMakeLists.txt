add_library(DDB_zkd STATIC
  ZkdHelper.cpp)

option(USE_FAST_DOUBLE_MEMCMP_ENCODING 
  "Used to trigger the use for a faster double encode"
  ON)

if (USE_FAST_DOUBLE_MEMCMP_ENCODING)
  target_compile_definitions(DDB_zkd PRIVATE USE_FAST_DOUBLE_MEMCMP_ENCODING)
endif ()

target_link_libraries(DDB_zkd arango)
