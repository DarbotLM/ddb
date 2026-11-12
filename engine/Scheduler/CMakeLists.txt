add_library(ddb_scheduler STATIC
  LockfreeThreadPool.cpp
  Scheduler.cpp
  SchedulerFeature.cpp
  SchedulerMetrics.cpp
  SimpleThreadPool.cpp
  SupervisedScheduler.cpp
  ThreadPoolScheduler.cpp
  WorkStealingThreadPool.cpp
)

target_link_libraries(ddb_scheduler
  PUBLIC
  velocypack
  ddb_futures
  ddb_basic_utils
  ddb_basic_errors
)

if (USE_V8)
  target_link_libraries(ddb_scheduler PUBLIC ${V8_LIBS})
  target_link_libraries(ddb_scheduler PUBLIC v8_interface)
endif()

