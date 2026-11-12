add_library(ddb_zkd STATIC
  ZkdHelper.cpp)

option(USE_FAST_DOUBLE_MEMCMP_ENCODING 
  "Used to trigger the use for a faster double encode"
  ON)

if (USE_FAST_DOUBLE_MEMCMP_ENCODING)
  target_compile_definitions(ddb_zkd PRIVATE USE_FAST_DOUBLE_MEMCMP_ENCODING)
endif ()

target_link_libraries(ddb_zkd arango)
