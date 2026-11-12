add_library(ddb_geo STATIC
  GeoJson.cpp
  GeoParams.cpp
  ShapeContainer.cpp
  Utils.cpp)

if (NOT USE_ENTERPRISE)
  target_sources(ddb_geo PRIVATE Coding.cpp)
endif ()

target_link_libraries(ddb_geo
  ddb_basic_utils
  boost_system
  boost_boost
  s2)

add_subdirectory(karney)
add_subdirectory(S2)
