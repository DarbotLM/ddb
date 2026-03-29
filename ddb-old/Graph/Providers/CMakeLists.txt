target_sources(DDB_graph PRIVATE
  ClusterProvider.cpp
  BaseProviderOptions.cpp
  SingleServerProvider.cpp
  ProviderTracer.cpp)

add_subdirectory(SingleServer)
