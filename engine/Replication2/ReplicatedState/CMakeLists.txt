target_sources(ddb_replication2 PRIVATE
  ReplicatedStateFeature.cpp
  ReplicatedStateMetrics.cpp
  StateCommon.cpp
  WaitForQueue.cpp)

target_sources(ddb_replication2_pure PRIVATE
  StateStatus.h
  StateCommon.cpp
  )
