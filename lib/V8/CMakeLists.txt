add_library(ddb_v8 STATIC
  V8PlatformFeature.cpp
  V8SecurityFeature.cpp
  JavaScriptSecurityContext.cpp
  JSLoader.cpp
  V8LineEditor.cpp
  v8-buffer.cpp
  v8-conv.cpp
  v8-globals.cpp
  v8-shell.cpp
  v8-utils.cpp
  v8-vpack.cpp
  v8-environment.cpp)
target_link_libraries(ddb_v8
  ddb_assertions
  ddb_static_strings
  ddb_futures
  ddb_basic_errors
  ddb_basic_utils
  ddb_basic_features
  ddb_basic_logger
  ddb_inspection
  boost_boost
  boost_system
  absl::flat_hash_set
  absl::synchronization
  ddb_async
  fmt::fmt
  v8_interface
  velocypack
  immer
)
target_include_directories(ddb_v8
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  SYSTEM PUBLIC
  ${OPENSSL_INCLUDE_DIR}
  ${ICU64_INCLUDE_DIR})

# Enterprise
if (USE_ENTERPRISE)
  target_include_directories(ddb_v8 PUBLIC "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
endif()

if (USE_V8)
  add_library(ddb_v8_no_deadline STATIC
    v8-no-deadline.cpp)
  target_link_libraries(ddb_v8_no_deadline
    arango
    v8_interface
    velocypack)
  target_include_directories(ddb_v8_no_deadline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
endif()
