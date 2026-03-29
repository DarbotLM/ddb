add_library(DDB_v8 STATIC
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
target_link_libraries(DDB_v8
  DDB_assertions
  DDB_static_strings
  DDB_futures
  DDB_basic_errors
  DDB_basic_utils
  DDB_basic_features
  DDB_basic_logger
  DDB_inspection
  boost_boost
  boost_system
  absl::flat_hash_set
  absl::synchronization
  DDB_async
  fmt::fmt
  v8_interface
  velocypack
  immer
)
target_include_directories(DDB_v8
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  SYSTEM PUBLIC
  ${OPENSSL_INCLUDE_DIR}
  ${ICU64_INCLUDE_DIR})

# Enterprise
if (USE_ENTERPRISE)
  target_include_directories(DDB_v8 PUBLIC "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
endif()

if (USE_V8)
  add_library(DDB_v8_no_deadline STATIC
    v8-no-deadline.cpp)
  target_link_libraries(DDB_v8_no_deadline
    arango
    v8_interface
    velocypack)
  target_include_directories(DDB_v8_no_deadline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
endif()
