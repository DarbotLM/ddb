add_library(DDB_scheduler STATIC
  LockfreeThreadPool.cpp
  Scheduler.cpp
  SchedulerFeature.cpp
  SchedulerMetrics.cpp
  SimpleThreadPool.cpp
  SupervisedScheduler.cpp
  ThreadPoolScheduler.cpp
  WorkStealingThreadPool.cpp
)

target_link_libraries(DDB_scheduler
  PUBLIC
  velocypack
  DDB_futures
  DDB_basic_utils
  DDB_basic_errors
)

if (USE_V8)
  target_link_libraries(DDB_scheduler PUBLIC ${V8_LIBS})
  target_link_libraries(DDB_scheduler PUBLIC v8_interface)
endif()

