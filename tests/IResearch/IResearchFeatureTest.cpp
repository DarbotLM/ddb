////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 darbotdb GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/darbotdb/darbotdb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is darbotdb GmbH, Cologne, Germany
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#include "gtest/gtest.h"

#include "utils/misc.hpp"
#include "utils/string.hpp"
#include "utils/thread_utils.hpp"
#include <filesystem>
#include "utils/version_defines.hpp"
#include "utils/file_utils.hpp"

#include "IResearch/common.h"
#include "Mocks/LogLevels.h"
#include "Mocks/Servers.h"
#include "Mocks/StorageEngineMock.h"
#include "Mocks/TemplateSpecializer.h"

#include "Agency/AsyncAgencyComm.h"
#include "Agency/Store.h"
#include "ApplicationFeatures/CommunicationFeaturePhase.h"
#include "Aql/AqlFunctionFeature.h"
#include "Basics/NumberOfCores.h"
#include "Cluster/AgencyCache.h"
#include "Cluster/ClusterFeature.h"
#include "Cluster/ClusterInfo.h"
#include "Cluster/ClusterTypes.h"
#include "ClusterEngine/ClusterEngine.h"
#include "GeneralServer/AuthenticationFeature.h"
#include "GeneralServer/ServerSecurityFeature.h"
#include "IResearch/ApplicationServerHelper.h"
#include "IResearch/Containers.h"
#include "IResearch/IResearchAnalyzerFeature.h"
#include "IResearch/IResearchCommon.h"
#include "IResearch/IResearchExecutionPool.h"
#include "IResearch/IResearchFeature.h"
#include "IResearch/IResearchLinkCoordinator.h"
#include "IResearch/IResearchLinkHelper.h"
#include "IResearch/IResearchView.h"
#include "Rest/Version.h"
#include "RestServer/DatabaseFeature.h"
#include "RestServer/DatabasePathFeature.h"
#include "RestServer/FlushFeature.h"
#include "Metrics/MetricsFeature.h"
#include "RestServer/QueryRegistryFeature.h"
#include "RestServer/SystemDatabaseFeature.h"
#include "RestServer/UpgradeFeature.h"
#include "RestServer/ViewTypesFeature.h"
#include "Sharding/ShardingFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "StorageEngine/PhysicalCollection.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/Methods/Indexes.h"
#include "VocBase/Methods/Upgrade.h"
#include "VocBase/Methods/Version.h"

using namespace std::chrono_literals;

// -----------------------------------------------------------------------------
// --SECTION--                                                 setup / tear-down
// -----------------------------------------------------------------------------

class IResearchFeatureTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AGENCY,
                                            darbotdb::LogLevel::FATAL>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::CLUSTER,
                                            darbotdb::LogLevel::FATAL> {
 protected:
  darbotdb::tests::mocks::MockV8Server server;

  IResearchFeatureTest() : server(false) {
    darbotdb::tests::init();

    server.addFeature<darbotdb::iresearch::IResearchAnalyzerFeature>(false);
    server.addFeature<darbotdb::FlushFeature>(false);
    server.addFeature<darbotdb::QueryRegistryFeature>(
        false, server.template getFeature<darbotdb::metrics::MetricsFeature>());
    server.addFeature<darbotdb::ServerSecurityFeature>(false);
    server.startFeatures();
  }

  // version 0 data-source path
  std::filesystem::path getPersistedPath0(darbotdb::LogicalView const& view) {
    auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
    std::filesystem::path dataPath(dbPathFeature.directory());
    dataPath /= "databases";
    dataPath /= "database-";
    dataPath += std::to_string(view.vocbase().id());
    dataPath /= darbotdb::iresearch::StaticStrings::ViewArangoSearchType;
    dataPath += "-";
    dataPath += std::to_string(view.id().id());
    return dataPath;
  }

  // version 1 data-source path
  std::filesystem::path getPersistedPath1(
      darbotdb::iresearch::IResearchLink const& link) {
    auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
    std::filesystem::path dataPath(dbPathFeature.directory());
    dataPath /= "databases";
    dataPath /= "database-";
    dataPath += std::to_string(link.index().collection().vocbase().id());
    dataPath /= darbotdb::iresearch::StaticStrings::ViewArangoSearchType;
    dataPath += "-";
    dataPath += std::to_string(link.index().collection().id().id());
    dataPath += "_";
    dataPath += std::to_string(link.index().id().id());
    return dataPath;
  }
};

// -----------------------------------------------------------------------------
// --SECTION--                                                        test suite
// -----------------------------------------------------------------------------

TEST_F(IResearchFeatureTest, test_options_default) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  auto* executeThreadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.execution-threads-limit");
  ASSERT_NE(nullptr, executeThreadsLimit);
  ASSERT_EQ(0, *executeThreadsLimit->ptr);

  auto* defaultParallelism =
      opts->get<UInt32Parameter>("--arangosearch.default-parallelism");
  ASSERT_NE(nullptr, defaultParallelism);
  ASSERT_EQ(1, *defaultParallelism->ptr);

  uint32_t const expectedNumThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedNumExecuteThreads =
      uint32_t(darbotdb::NumberOfCores::getValue()) * 2;
  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedNumThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedNumThreads, *commitThreads->ptr);
  ASSERT_EQ(expectedNumExecuteThreads, *executeThreadsLimit->ptr);
  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_0);
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_commit_threads_default_set) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads = expectedConsolidationThreads;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = 0;

  auto* executeThreadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.execution-threads-limit");
  ASSERT_NE(nullptr, executeThreadsLimit);
  ASSERT_EQ(0, *executeThreadsLimit->ptr);
  *executeThreadsLimit->ptr = 0;
  opts->processingResult().touch("arangosearch.execution-threads-limit");

  auto* defaultParallelism =
      opts->get<UInt32Parameter>("--arangosearch.default-parallelism");
  ASSERT_NE(nullptr, defaultParallelism);
  *defaultParallelism->ptr = 5;
  opts->processingResult().touch("arangosearch.default-parallelism");

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);
  ASSERT_EQ(0, *executeThreadsLimit->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(5, feature.defaultParallelism());
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_commit_threads_min) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads = 1;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = expectedCommitThreads;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_commit_threads) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads = 6;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_consolidation_threads) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedCommitThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedConsolidationThreads = 6;

  opts->processingResult().touch("arangosearch.consolidation-threads");
  *consolidationThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_consolidation_threads_idle_auto) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedCommitThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedConsolidationThreads = 6;

  opts->processingResult().touch("arangosearch.consolidation-threads");
  *consolidationThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_consolidation_threads_idle_set) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedCommitThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedConsolidationThreads = 6;

  opts->processingResult().touch("arangosearch.consolidation-threads");
  *consolidationThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest,
       test_options_consolidation_threads_idle_set_to_zero) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedCommitThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedConsolidationThreads = 6;

  opts->processingResult().touch("arangosearch.consolidation-threads");
  *consolidationThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(
    IResearchFeatureTest,
    test_options_consolidation_threads_idle_greater_than_consolidation_threads) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedCommitThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedConsolidationThreads = 6;

  opts->processingResult().touch("arangosearch.consolidation-threads");
  *consolidationThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_commit_threads_idle_auto) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads = 6;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_commit_threads_idle_set) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads = 6;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest,
       test_options_commit_threads_idle_greater_than_commit_threads) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads = 6;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = 6;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_custom_thread_count) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads = 4;
  uint32_t const expectedCommitThreads = 6;

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = expectedCommitThreads;
  opts->processingResult().touch("arangosearch.consolidation-threads");
  *consolidationThreads->ptr = expectedConsolidationThreads;

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_commit_threads_max) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  uint32_t const expectedConsolidationThreads =
      std::max(1U, uint32_t(darbotdb::NumberOfCores::getValue()) / 6);
  uint32_t const expectedCommitThreads =
      4 * uint32_t(darbotdb::NumberOfCores::getValue());

  opts->processingResult().touch("arangosearch.commit-threads");
  *commitThreads->ptr = std::numeric_limits<uint32_t>::max();

  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedConsolidationThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedCommitThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedCommitThreads)),
      ThreadGroup::_0);
  waitForStats(std::make_tuple(size_t(0), size_t(0),
                               size_t(expectedConsolidationThreads)),
               ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_threads_set_zero) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  opts->processingResult().touch("arangosearch.threads");

  uint32_t const expectedNumThreads = std::max(
      1U, std::min(4U, (uint32_t(darbotdb::NumberOfCores::getValue()) / 8)));
  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedNumThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedNumThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_0);
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_threads) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  opts->processingResult().touch("arangosearch.threads");
  *threads->ptr = 3;

  uint32_t const expectedNumThreads = *threads->ptr / 2;
  feature.validateOptions(opts);
  ASSERT_EQ(3, *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedNumThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedNumThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_0);
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_threads_max) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  opts->processingResult().touch("arangosearch.threads");
  *threads->ptr = std::numeric_limits<uint32_t>::max();

  uint32_t const expectedNumThreads = 8 / 2;
  feature.validateOptions(opts);
  ASSERT_EQ(std::numeric_limits<uint32_t>::max(), *threads->ptr);
  ASSERT_EQ(0, *threadsLimit->ptr);
  ASSERT_EQ(expectedNumThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedNumThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_0);
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_options_threads_limit_max) {
  using namespace darbotdb::options;
  using namespace darbotdb::iresearch;

  IResearchFeature feature(server.server());
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));

  auto waitForStats = [&](std::tuple<size_t, size_t, size_t> expectedStats,
                          darbotdb::iresearch::ThreadGroup group,
                          std::chrono::steady_clock::duration timeout = 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (expectedStats != feature.stats(group)) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  feature.collectOptions(opts);
  auto* threads = opts->get<UInt32Parameter>("--arangosearch.threads");
  ASSERT_NE(nullptr, threads);
  ASSERT_EQ(0, *threads->ptr);
  auto* threadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.threads-limit");
  ASSERT_NE(nullptr, threadsLimit);
  ASSERT_EQ(0, *threadsLimit->ptr);
  auto* consolidationThreads =
      opts->get<UInt32Parameter>("--arangosearch.consolidation-threads");
  ASSERT_NE(nullptr, consolidationThreads);
  ASSERT_EQ(0, *consolidationThreads->ptr);
  auto* commitThreads =
      opts->get<UInt32Parameter>("--arangosearch.commit-threads");
  ASSERT_NE(nullptr, commitThreads);
  ASSERT_EQ(0, *commitThreads->ptr);

  opts->processingResult().touch("arangosearch.threads-limit");
  *threadsLimit->ptr = 1;

  uint32_t const expectedNumThreads = 1;
  feature.validateOptions(opts);
  ASSERT_EQ(0, *threads->ptr);
  ASSERT_EQ(1, *threadsLimit->ptr);
  ASSERT_EQ(expectedNumThreads, *consolidationThreads->ptr);
  ASSERT_EQ(expectedNumThreads, *commitThreads->ptr);

  feature.prepare();
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(0), size_t(0)),
            feature.limits(ThreadGroup::_1));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));

  feature.start();
  ASSERT_EQ(
      std::make_pair(size_t(*commitThreads->ptr), size_t(*commitThreads->ptr)),
      feature.limits(ThreadGroup::_0));
  ASSERT_EQ(std::make_pair(size_t(*consolidationThreads->ptr),
                           size_t(*consolidationThreads->ptr)),
            feature.limits(ThreadGroup::_1));
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_0);
  waitForStats(
      std::make_tuple(size_t(0), size_t(0), size_t(expectedNumThreads)),
      ThreadGroup::_1);
  feature.stop();
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            feature.stats(ThreadGroup::_1));
}

TEST_F(IResearchFeatureTest, test_execution_threads_limit) {
  using namespace darbotdb;
  using namespace darbotdb::iresearch;
  using namespace darbotdb::options;
  constexpr uint32_t threadsLimit = 10;
  IResearchFeature iresearch(server.server());
  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  iresearch.collectOptions(opts);
  auto* executeThreadsLimit =
      opts->get<UInt32Parameter>("--arangosearch.execution-threads-limit");
  *executeThreadsLimit->ptr = threadsLimit;
  opts->processingResult().touch("arangosearch.execution-threads-limit");
  iresearch.validateOptions(opts);
  iresearch.prepare();
  iresearch.start();
  auto& metricsFeature = server.server().getFeature<metrics::MetricsFeature>();
  metrics::MetricKeyView key{.name = "darbotdb_search_execution_threads_demand",
                             .labels = ""};
  auto* metricValue = metricsFeature.get(key);
  ASSERT_NE(nullptr, metricValue);
  auto gauge = static_cast<metrics::Gauge<uint64_t>*>(metricValue);
  ASSERT_EQ(0, gauge->load());
  auto& pool = iresearch.getSearchPool();
  ASSERT_EQ(threadsLimit, pool.allocateThreads(threadsLimit, threadsLimit));
  ASSERT_EQ(0, pool.allocateThreads(1, 1));
  ASSERT_EQ(threadsLimit + 1, gauge->load());
  ASSERT_EQ(0, pool.allocateThreads(1, 0));
  ASSERT_EQ(threadsLimit + 1, gauge->load());

  pool.releaseThreads(threadsLimit, threadsLimit + 1);
  ASSERT_EQ(0, gauge->load());
  ASSERT_EQ(5, pool.allocateThreads(5, 5));
  ASSERT_EQ(5, gauge->load());
  ASSERT_EQ(5, pool.allocateThreads(5, 5));
  ASSERT_EQ(10, gauge->load());
  ASSERT_EQ(0, pool.allocateThreads(1, 0));
  pool.releaseThreads(10, 10);
  ASSERT_EQ(0, gauge->load());
  ASSERT_EQ(6, pool.allocateThreads(6, 6));
  ASSERT_EQ(6, gauge->load());
  ASSERT_EQ(4, pool.allocateThreads(6, 6));
  ASSERT_EQ(12, gauge->load());
  pool.releaseThreads(10, 12);
  ASSERT_EQ(0, gauge->load());
  constexpr size_t iterations = 10000;
  std::atomic<bool> exceeded{false};
  auto threadFunc1 = [&]() {
    uint64_t allocated = 0;
    uint64_t demand = 0;
    for (size_t i = 0; i < iterations && !exceeded; ++i) {
      auto tmp = pool.allocateThreads(1, 1);
      allocated += tmp;
      ++demand;
      if (allocated > threadsLimit) {
        exceeded = true;
      }
      if (allocated && (allocated == threadsLimit || tmp == 0)) {
        pool.releaseThreads(allocated, demand);
        allocated = 0;
        demand = 0;
      }
    }
    if (allocated || demand) {
      pool.releaseThreads(allocated, demand);
    }
  };

  auto threadFunc2 = [&]() {
    uint64_t allocated = 0;
    uint64_t demand = 0;
    for (size_t i = 0; i < iterations && !exceeded; ++i) {
      auto tmp = pool.allocateThreads(threadsLimit / 3, threadsLimit / 3);
      allocated += tmp;
      demand += threadsLimit / 3;
      if (allocated > threadsLimit) {
        exceeded = true;
        break;
      }
      if (allocated && (allocated == threadsLimit || tmp == 0)) {
        pool.releaseThreads(allocated, demand);
        allocated = 0;
        demand = 0;
      }
    }
    if (allocated || demand) {
      pool.releaseThreads(allocated, demand);
    }
  };

  std::thread t1(threadFunc1);
  std::thread t2(threadFunc2);
  std::thread t3(threadFunc1);
  std::thread t4(threadFunc2);
  t1.join();
  t2.join();
  t3.join();
  t4.join();
  iresearch.stop();
  ASSERT_FALSE(exceeded.load());
  ASSERT_EQ(0, pool.load());
}

TEST_F(IResearchFeatureTest, test_start) {
  using namespace darbotdb;
  using namespace darbotdb::iresearch;
  using namespace darbotdb::options;

  auto& functions = server.addFeatureUntracked<aql::AqlFunctionFeature>();
  auto& iresearch = server.addFeatureUntracked<IResearchFeature>();
  irs::Finally cleanup = [&functions]() noexcept { functions.unprepare(); };

  auto waitForNewStats = [&](std::tuple<size_t, size_t, size_t> oldStats,
                             darbotdb::iresearch::ThreadGroup group,
                             std::chrono::steady_clock::duration timeout =
                                 10s) {
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (oldStats == iresearch.stats(group)) {
      std::this_thread::sleep_for(10ms);
      if (end < std::chrono::steady_clock::now()) {
        break;
      }
    }
    ASSERT_NE(oldStats, iresearch.stats(group));
  };

  enum class FunctionType { FILTER = 0, SCORER };

  std::map<std::string_view, std::pair<std::string_view, FunctionType>>
      expected = {
          // filter functions
          {"EXISTS", {".|.,.", FunctionType::FILTER}},
          {"PHRASE", {".,.|.+", FunctionType::FILTER}},
          {"STARTS_WITH", {".,.|.,.", FunctionType::FILTER}},
          {"MIN_MATCH", {".,.|.+", FunctionType::FILTER}},
          {"LIKE", {".,.|.", FunctionType::FILTER}},
          {"NGRAM_MATCH", {".,.|.,.", FunctionType::FILTER}},
          {"LEVENSHTEIN_MATCH", {".,.,.|.,.,.", FunctionType::FILTER}},
          {"IN_RANGE", {".,.,.,.,.", FunctionType::FILTER}},
          {"GEO_IN_RANGE", {".,.,.,.|.,.,.", FunctionType::FILTER}},
          {"GEO_CONTAINS", {".,.", FunctionType::FILTER}},
          {"GEO_INTERSECTS", {".,.", FunctionType::FILTER}},

          // context functions
          {"ANALYZER", {".,.", FunctionType::FILTER}},
          {"BOOST", {".,.", FunctionType::FILTER}},

          // scorer functions
          {"BM25", {".|+", FunctionType::SCORER}},
          {"TFIDF", {".|+", FunctionType::SCORER}},
      };

  auto opts = std::make_shared<ProgramOptions>("", "", "", "");
  iresearch.collectOptions(opts);
  iresearch.validateOptions(opts);

  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            iresearch.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            iresearch.stats(ThreadGroup::_1));

  for (auto& entry : expected) {
    auto* function = darbotdb::iresearch::getFunction(functions, entry.first);
    EXPECT_EQ(nullptr, function);
  }

  functions.prepare();
  iresearch.prepare();

  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            iresearch.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            iresearch.stats(ThreadGroup::_1));

  iresearch.start();
  waitForNewStats(std::make_tuple(size_t(0), size_t(0), size_t(0)),
                  ThreadGroup::_0);
  waitForNewStats(std::make_tuple(size_t(0), size_t(0), size_t(0)),
                  ThreadGroup::_1);

  for (auto& entry : expected) {
    auto* function = darbotdb::iresearch::getFunction(functions, entry.first);
    EXPECT_NE(nullptr, function);
    EXPECT_EQ(entry.second.first, function->arguments);
    EXPECT_TRUE((entry.second.second == FunctionType::FILTER &&
                 darbotdb::iresearch::isFilter(*function)) ||
                (entry.second.second == FunctionType::SCORER &&
                 darbotdb::iresearch::isScorer(*function)));
  }

  iresearch.stop();

  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            iresearch.stats(ThreadGroup::_0));
  ASSERT_EQ(std::make_tuple(size_t(0), size_t(0), size_t(0)),
            iresearch.stats(ThreadGroup::_1));

  functions.unprepare();
}

TEST_F(IResearchFeatureTest, test_upgrade0_1_no_directory) {
  // test single-server (no directory)
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testCollection\" }");
  auto linkJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"view\": \"testView\", \"type\": \"arangosearch\", "
      "\"includeAllFields\": true }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testView\", \"type\": \"arangosearch\", \"version\": 0 "
      "}");
  auto versionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"version\": 0, \"tasks\": {} }");

  // add the UpgradeFeature, but make sure it is not prepared
  server.addFeatureUntracked<darbotdb::UpgradeFeature>(nullptr,
                                                       std::vector<size_t>{});

  auto& feature =
      server.addFeatureUntracked<darbotdb::iresearch::IResearchFeature>();
  feature.collectOptions(server.server().options());
  feature.validateOptions(server.server().options());
  feature.prepare();  // register iresearch view type
  feature.start();    // register upgrade tasks

  // skip IResearchView validation
  server.getFeature<darbotdb::DatabaseFeature>().enableUpgrade();

  auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();

  // ensure test data is stored in a unique directory
  darbotdb::tests::setDatabasePath(dbPathFeature);
  auto versionFilename = StorageEngineMock::versionFilenameResult;
  irs::Finally versionFilenameRestore = [&versionFilename]() noexcept {
    StorageEngineMock::versionFilenameResult = versionFilename;
  };
  StorageEngineMock::versionFilenameResult =
      (std::filesystem::path(dbPathFeature.directory()) /= "version").string();
  ASSERT_TRUE(irs::file_utils::mkdir(
      std::filesystem::path(dbPathFeature.directory()).c_str(), true));
  ASSERT_TRUE((darbotdb::basics::VelocyPackHelper::velocyPackToFile(
      StorageEngineMock::versionFilenameResult, versionJson->slice(), false)));

  TRI_vocbase_t vocbase(testDBInfo(server.server()));
  auto logicalCollection = vocbase.createCollection(collectionJson->slice());
  ASSERT_NE(logicalCollection, nullptr);
  auto logicalView0 = vocbase.createView(viewJson->slice(), false);
  // logicalView0->guid();
  ASSERT_NE(logicalView0, nullptr);
  bool created = false;
  auto index =
      logicalCollection->createIndex(linkJson->slice(), created).waitAndGet();
  ASSERT_TRUE(created);
  ASSERT_NE(index, nullptr);
  auto link0 =
      std::dynamic_pointer_cast<darbotdb::iresearch::IResearchLink>(index);
  ASSERT_NE(link0, nullptr);

  index->unload();  // release file handles
  bool result;
  auto linkDataPath = getPersistedPath1(*link0);
  EXPECT_TRUE(
      irs::file_utils::remove(linkDataPath.c_str()));  // remove link directory
  auto viewDataPath = getPersistedPath0(*logicalView0);
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure no view directory
  darbotdb::velocypack::Builder builder;
  builder.openObject();
  EXPECT_TRUE(
      logicalView0
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(0,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 0 before upgrade

  EXPECT_TRUE(darbotdb::methods::Upgrade::startup(vocbase, true, false)
                  .ok());  // run upgrade
  auto logicalView1 = vocbase.lookupView(logicalView0->name());
  EXPECT_FALSE(!logicalView1);  // ensure view present after upgrade
  EXPECT_EQ(logicalView0->id(), logicalView1->id());  // ensure same id for view
  auto link1 = darbotdb::iresearch::IResearchLinkHelper::find(
      *logicalCollection, *logicalView1);
  ASSERT_NE(nullptr, link1);  // ensure link present after upgrade
  EXPECT_NE(link0->index().id(), link1->index().id());  // ensure new link
  linkDataPath = getPersistedPath1(*link1);
  EXPECT_TRUE(irs::file_utils::exists(result, linkDataPath.c_str()) &&
              result);  // ensure link directory created after upgrade
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure view directory not present
  viewDataPath = getPersistedPath0(*logicalView1);
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure view directory not created
  builder.clear();
  builder.openObject();
  EXPECT_TRUE(
      logicalView1
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(1,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 1 after upgrade
}

TEST_F(IResearchFeatureTest, test_upgrade0_1_with_directory) {
  // test single-server (with directory)
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testCollection\" }");
  auto linkJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"view\": \"testView\", \"type\": \"arangosearch\", "
      "\"includeAllFields\": true }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testView\", \"type\": \"arangosearch\", \"version\": 0 "
      "}");
  auto versionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"version\": 0, \"tasks\": {} }");

  // add the UpgradeFeature, but make sure it is not prepared
  server.addFeatureUntracked<darbotdb::UpgradeFeature>(nullptr,
                                                       std::vector<size_t>{});

  auto& feature =
      server.addFeatureUntracked<darbotdb::iresearch::IResearchFeature>();
  feature.collectOptions(server.server().options());
  feature.validateOptions(server.server().options());
  feature.prepare();  // register iresearch view type
  feature.start();    // register upgrade tasks

  server.getFeature<darbotdb::DatabaseFeature>()
      .enableUpgrade();  // skip IResearchView validation

  auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
  darbotdb::tests::setDatabasePath(
      dbPathFeature);  // ensure test data is stored in a unique directory
  auto versionFilename = StorageEngineMock::versionFilenameResult;
  irs::Finally versionFilenameRestore = [&versionFilename]() noexcept {
    StorageEngineMock::versionFilenameResult = versionFilename;
  };
  StorageEngineMock::versionFilenameResult =
      (std::filesystem::path(dbPathFeature.directory()) /= "version").string();
  ASSERT_TRUE(irs::file_utils::mkdir(
      std::filesystem::path(dbPathFeature.directory()).c_str(), true));
  ASSERT_TRUE((darbotdb::basics::VelocyPackHelper::velocyPackToFile(
      StorageEngineMock::versionFilenameResult, versionJson->slice(), false)));

  TRI_vocbase_t vocbase(testDBInfo(server.server()));
  auto logicalCollection = vocbase.createCollection(collectionJson->slice());
  ASSERT_FALSE(!logicalCollection);
  auto logicalView0 = vocbase.createView(viewJson->slice(), false);
  ASSERT_FALSE(!logicalView0);
  bool created;
  auto index =
      logicalCollection->createIndex(linkJson->slice(), created).waitAndGet();
  ASSERT_TRUE(created);
  ASSERT_FALSE(!index);
  auto link0 =
      std::dynamic_pointer_cast<darbotdb::iresearch::IResearchLink>(index);
  ASSERT_FALSE(!link0);

  index->unload();  // release file handles
  bool result;
  auto linkDataPath = getPersistedPath1(*link0);
  EXPECT_TRUE(
      irs::file_utils::remove(linkDataPath.c_str()));  // remove link directory
  auto viewDataPath = getPersistedPath0(*logicalView0);
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) && !result);
  EXPECT_TRUE(irs::file_utils::mkdir(viewDataPath.c_str(),
                                     true));  // create view directory
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) && result);
  darbotdb::velocypack::Builder builder;
  builder.openObject();
  EXPECT_TRUE(
      logicalView0
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(0,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 0 before upgrade

  EXPECT_TRUE(darbotdb::methods::Upgrade::startup(vocbase, true, false)
                  .ok());  // run upgrade
  auto logicalView1 = vocbase.lookupView(logicalView0->name());
  EXPECT_FALSE(!logicalView1);  // ensure view present after upgrade
  EXPECT_EQ(logicalView0->id(), logicalView1->id());  // ensure same id for view
  auto link1 = darbotdb::iresearch::IResearchLinkHelper::find(
      *logicalCollection, *logicalView1);
  EXPECT_FALSE(!link1);  // ensure link present after upgrade
  EXPECT_NE(link0->index().id(), link1->index().id());  // ensure new link
  linkDataPath = getPersistedPath1(*link1);
  EXPECT_TRUE(irs::file_utils::exists(result, linkDataPath.c_str()) &&
              result);  // ensure link directory created after upgrade
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure view directory removed after upgrade
  viewDataPath = getPersistedPath0(*logicalView1);
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure view directory not created
  builder.clear();
  builder.openObject();
  EXPECT_TRUE(
      logicalView1
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(1,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 1 after upgrade
}

TEST_F(IResearchFeatureTest, IResearch_version_test) {
  EXPECT_EQ(IResearch_version, darbotdb::rest::Version::getIResearchVersion());
  EXPECT_TRUE(IResearch_version ==
              darbotdb::rest::Version::Values["iresearch-version"]);
}

TEST_F(IResearchFeatureTest, test_async_schedule_wait_indefinite) {
  struct Task {
    Task(std::atomic_bool& deallocated, std::mutex& mutex,
         std::condition_variable& cond, std::atomic<size_t>& count,
         darbotdb::iresearch::IResearchFeature& feature)
        : flag(&deallocated, [](std::atomic_bool* ptr) { *ptr = true; }),
          mutex(&mutex),
          cond(&cond),
          count(&count),
          feature(&feature) {}

    void operator()() {
      ++*count;

      std::lock_guard scopedLock{*mutex};
      feature->queue(darbotdb::iresearch::ThreadGroup::_1, 10000ms, *this);
      cond->notify_all();
    }

    std::shared_ptr<std::atomic_bool> flag;
    std::mutex* mutex;
    std::condition_variable* cond;
    std::atomic<size_t>* count;
    darbotdb::iresearch::IResearchFeature* feature;
  };

  std::atomic_bool deallocated = false;
  // declare above 'feature' to ensure proper destruction order
  darbotdb::iresearch::IResearchFeature feature(server.server());
  feature.collectOptions(server.server().options());
  server.server()
      .options()
      ->get<darbotdb::options::UInt32Parameter>(
          "arangosearch.consolidation-threads")
      ->set("1");
  feature.validateOptions(server.server().options());
  feature.prepare();
  feature.start();  // start thread pool
  std::condition_variable cond;
  std::mutex mutex;
  std::atomic<size_t> count = 0;

  std::unique_lock lock{mutex};
  feature.queue(darbotdb::iresearch::ThreadGroup::_1, 0ms,
                Task(deallocated, mutex, cond, count, feature));

  {
    auto const end = std::chrono::steady_clock::now() + 10s;
    while (!count) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  }

  EXPECT_EQ(1, count);
  EXPECT_NE(std::cv_status::timeout,
            cond.wait_for(lock, 1000ms));  // first run invoked immediately
  EXPECT_FALSE(deallocated);

  {
    auto const end = std::chrono::steady_clock::now() + 10s;
    while (!std::get<1>(feature.stats(darbotdb::iresearch::ThreadGroup::_1))) {
      std::this_thread::sleep_for(10ms);
      ASSERT_LE(std::chrono::steady_clock::now(), end);
    }
  }

  std::cv_status wait_status;
  do {
    wait_status = cond.wait_for(lock, 100ms);
    if (std::cv_status::timeout == wait_status) {
      break;
    }
    ASSERT_EQ(1, count);  // spurious wakeup?
  } while (1);
  EXPECT_FALSE(deallocated);  // still scheduled
  EXPECT_EQ(1, count);
}

TEST_F(IResearchFeatureTest, test_async_single_run_task) {
  bool deallocated = false;
  // declare above 'feature' to ensure proper destruction order
  darbotdb::iresearch::IResearchFeature feature(server.server());
  feature.collectOptions(server.server().options());
  feature.validateOptions(server.server().options());
  feature.prepare();
  feature.start();  // start thread pool
  std::condition_variable cond;
  std::mutex mutex;
  struct Func {
    bool& _deallocated;
    std::condition_variable& _cond;
    std::mutex& _mutex;
  };
  Func func{deallocated, cond, mutex};
  std::shared_ptr<Func> flag(&func, [](Func* func) {
    std::lock_guard lock{func->_mutex};
    func->_deallocated = true;
    func->_cond.notify_all();
  });
  feature.queue(darbotdb::iresearch::ThreadGroup::_0, 0ms,
                [flag = std::move(flag)] {});
  std::unique_lock lock{mutex};
  ASSERT_TRUE(cond.wait_for(lock, 10s, [&] { return deallocated; }));
}

TEST_F(IResearchFeatureTest, test_async_multi_run_task) {
  std::atomic_bool deallocated = false;
  // declare above 'feature' to ensure proper destruction order
  darbotdb::iresearch::IResearchFeature feature(server.server());
  feature.collectOptions(server.server().options());
  feature.validateOptions(server.server().options());
  feature.prepare();
  feature.start();  // start thread pool
  std::mutex mutex;
  std::condition_variable cond;
  size_t count = 0;
  std::chrono::steady_clock::duration diff;
  std::unique_lock lock{mutex};

  {
    std::shared_ptr<std::atomic_bool> flag(
        &deallocated, [](std::atomic_bool* ptr) { *ptr = true; });

    struct Task {
      std::shared_ptr<std::atomic_bool> flag;
      size_t* count;
      std::chrono::steady_clock::duration* diff;
      std::mutex* mutex;
      std::condition_variable* cond;
      darbotdb::iresearch::IResearchFeature* feature;
      std::chrono::steady_clock::time_point last =
          std::chrono::steady_clock::now();

      void operator()() {
        *diff = std::chrono::steady_clock::now() - last;
        last = std::chrono::steady_clock::now();
        if (++(*count) <= 1) {
          feature->queue(darbotdb::iresearch::ThreadGroup::_0, 100ms, *this);
          return;
        }
        std::lock_guard scopedLock{*mutex};
        cond->notify_all();
      }
    };

    Task task;
    task.mutex = &mutex;
    task.cond = &cond;
    task.feature = &feature;
    task.count = &count;
    task.diff = &diff;
    task.flag = std::move(flag);

    feature.queue(darbotdb::iresearch::ThreadGroup::_0, 0ms, task);
  }

  EXPECT_NE(std::cv_status::timeout, cond.wait_for(lock, 1000ms));
  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(deallocated);  // TODO write correct sync
  EXPECT_EQ(2, count);
  EXPECT_TRUE(100ms < diff);
}

TEST_F(IResearchFeatureTest, test_async_deallocate_with_running_tasks) {
  std::atomic_bool deallocated = false;
  std::condition_variable cond;
  std::mutex mutex;
  std::unique_lock lock{mutex};

  {
    darbotdb::iresearch::IResearchFeature feature(server.server());
    feature.collectOptions(server.server().options());
    feature.validateOptions(server.server().options());
    feature.prepare();
    feature.start();  // start thread pool
    std::shared_ptr<std::atomic_bool> flag(
        &deallocated, [](std::atomic_bool* ptr) { *ptr = true; });

    struct Task {
      std::shared_ptr<std::atomic_bool> flag;
      std::mutex* mutex;
      std::condition_variable* cond;
      darbotdb::iresearch::IResearchFeature* feature;

      void operator()() {
        std::lock_guard lock{*mutex};
        cond->notify_all();

        feature->queue(darbotdb::iresearch::ThreadGroup::_0, 100ms, *this);
      }
    };

    Task task;
    task.mutex = &mutex;
    task.cond = &cond;
    task.feature = &feature;
    task.flag = std::move(flag);

    feature.queue(darbotdb::iresearch::ThreadGroup::_0, 0ms, task);

    EXPECT_NE(std::cv_status::timeout, cond.wait_for(lock, 100ms));
  }

  EXPECT_TRUE(deallocated);
}

TEST_F(IResearchFeatureTest, test_async_schedule_task_resize_pool) {
  std::atomic_bool deallocated = false;
  // declare above 'feature' to ensure proper destruction order
  darbotdb::iresearch::IResearchFeature feature(server.server());
  feature.collectOptions(server.server().options());
  server.server()
      .options()
      ->get<darbotdb::options::UInt32Parameter>("arangosearch.threads")
      ->set("8");
  feature.validateOptions(server.server().options());
  feature.prepare();
  std::condition_variable cond;
  std::mutex mutex;
  size_t count = 0;
  std::chrono::steady_clock::duration diff;
  std::unique_lock lock{mutex};
  {
    std::shared_ptr<std::atomic_bool> flag(
        &deallocated, [](std::atomic_bool* ptr) { *ptr = true; });

    struct Task {
      std::shared_ptr<std::atomic_bool> flag;
      size_t* count;
      std::chrono::steady_clock::duration* diff;
      std::mutex* mutex;
      std::condition_variable* cond;
      darbotdb::iresearch::IResearchFeature* feature;
      std::chrono::steady_clock::time_point last =
          std::chrono::steady_clock::now();

      void operator()() {
        *diff = std::chrono::steady_clock::now() - last;
        last = std::chrono::steady_clock::now();
        if (++(*count) <= 1) {
          feature->queue(darbotdb::iresearch::ThreadGroup::_0, 100ms, *this);
          return;
        }
        std::lock_guard lock{*mutex};
        cond->notify_all();
      }
    };

    Task task;
    task.mutex = &mutex;
    task.cond = &cond;
    task.feature = &feature;
    task.count = &count;
    task.diff = &diff;
    task.flag = flag;

    feature.queue(darbotdb::iresearch::ThreadGroup::_0, 0ms, task);
  }
  feature.start();  // start thread pool after a task has been scheduled, to
                    // trigger resize with a task
  EXPECT_NE(std::cv_status::timeout, cond.wait_for(lock, 1000ms));
  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(deallocated);  // TODO write correct sync
  EXPECT_EQ(2, count);
  EXPECT_TRUE(100ms < diff);
}

class IResearchFeatureTestCoordinator
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AGENCY,
                                            darbotdb::LogLevel::FATAL>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::CLUSTER,
                                            darbotdb::LogLevel::FATAL> {
 protected:
  darbotdb::tests::mocks::MockCoordinator server;

 private:
 protected:
  IResearchFeatureTestCoordinator() : server("CRDN_0001", false) {
    darbotdb::tests::init();

    darbotdb::ServerState::instance()->setRebootId(
        darbotdb::RebootId{1});  // Hack.
    // we will start Upgrade feature under our control
    server.untrackFeature<darbotdb::UpgradeFeature>();
    server.startFeatures();
  }

  darbotdb::consensus::index_t agencyTrx(std::string const& key,
                                         std::string const& value) {
    // Build an agency transaction:
    auto b2 = VPackParser::fromJson(value);
    VPackBuilder b;
    {
      VPackArrayBuilder trxs(&b);
      {
        VPackArrayBuilder trx(&b);
        {
          VPackObjectBuilder op(&b);
          b.add(key, b2->slice());
        }
      }
    }
    return std::get<1>(server.getFeature<darbotdb::ClusterFeature>()
                           .agencyCache()
                           .applyTestTransaction(b.slice()));
  }

  void agencyCreateDatabase(std::string const& name) {
    TemplateSpecializer ts(
        name, [&]() -> std::uint64_t { return server.genUniqId(); });
    std::string st = ts.specialize(plan_dbs_string);
    agencyTrx("/arango/Plan/Databases/" + name, st);
    st = ts.specialize(plan_colls_string);
    agencyTrx("/arango/Plan/Collections/" + name, st);
    st = ts.specialize(current_dbs_string);
    agencyTrx("/arango/Current/Databases/" + name, st);
    st = ts.specialize(current_colls_string);
    agencyTrx("/arango/Current/Collections/" + name, st);
    server.getFeature<darbotdb::ClusterFeature>()
        .clusterInfo()
        .waitForPlan(
            agencyTrx("/arango/Plan/Version", R"=({"op":"increment"})="))
        .wait();
    server.getFeature<darbotdb::ClusterFeature>()
        .clusterInfo()
        .waitForCurrent(
            agencyTrx("/arango/Current/Version", R"=({"op":"increment"})="))
        .wait();
  }

  void agencyDropDatabase(std::string const& name) {
    std::string st = R"=({"op":"delete"}))=";
    agencyTrx("/arango/Plan/Databases/" + name, st);
    agencyTrx("/arango/Plan/Collections/" + name, st);
    agencyTrx("/arango/Current/Databases/" + name, st);
    agencyTrx("/arango/Current/Collections/" + name, st);
    server.getFeature<darbotdb::ClusterFeature>()
        .clusterInfo()
        .waitForPlan(
            agencyTrx("/arango/Plan/Version", R"=({"op":"increment"})="))
        .wait();
    server.getFeature<darbotdb::ClusterFeature>()
        .clusterInfo()
        .waitForCurrent(
            agencyTrx("/arango/Current/Version", R"=({"op":"increment"})="))
        .wait();
  }

  VPackBuilder agencyCreateIndex(std::string const& db, std::string const& cid,
                                 std::set<std::string> const& fields,
                                 bool deduplicate, uint64_t id,
                                 std::string const& name, bool sparse,
                                 std::string const& type, bool unique) {
    VPackBuilder b;
    {
      VPackObjectBuilder o(&b);
      b.add(VPackValue(std::string("/arango/Plan/Collections/") + db + "/" +
                       cid + "/indexes"));
      {
        VPackObjectBuilder oo(&b);
        b.add("op", VPackValue("push"));
        b.add(VPackValue("new"));
        {
          VPackObjectBuilder ooo(&b);
          b.add(VPackValue("fields"));
          {
            VPackArrayBuilder aa(&b);
            for (auto const& i : fields) {
              b.add(VPackValue(i));
            }
          }
          b.add("deduplicate", VPackValue(deduplicate));
          b.add("id", VPackValue(id));
          b.add("inBackground", VPackValue(false));
          b.add("name", VPackValue(name));
          b.add("sparse", VPackValue(sparse));
          b.add("type", VPackValue(type));
          b.add("unique", VPackValue(unique));
        }
      }
    }
    return b;
  }
};

TEST_F(IResearchFeatureTestCoordinator, test_upgrade0_1) {
  // test coordinator
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"id\": \"41\", \"name\": \"testCollection\", \"shards\":{} }");
  auto linkJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"view\": \"testView\", \"type\": \"arangosearch\", "
      "\"includeAllFields\": true }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"id\": 42, \"name\": \"testView\", \"type\": \"arangosearch\", "
      "\"version\": 0 }");
  auto versionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"version\": 0, \"tasks\": {} }");
  auto collectionId = std::to_string(41);
  auto viewId = std::to_string(42);

  server.getFeature<darbotdb::DatabaseFeature>()
      .enableUpgrade();  // skip IResearchView validation

  auto& engine = server.getFeature<darbotdb::EngineSelectorFeature>().engine();
  auto& factory = server.getFeature<darbotdb::iresearch::IResearchFeature>()
                      .factory<darbotdb::ClusterEngine>();
  const_cast<darbotdb::IndexFactory&>(engine.indexFactory())
      .emplace(
          std::string{darbotdb::iresearch::StaticStrings::ViewArangoSearchType},
          factory);
  auto& ci = server.getFeature<darbotdb::ClusterFeature>().clusterInfo();
  TRI_vocbase_t* vocbase;  // will be owned by DatabaseFeature

  auto& database = server.getFeature<darbotdb::DatabaseFeature>();
  ASSERT_TRUE(
      database.createDatabase(testDBInfo(server.server()), vocbase).ok());

  agencyCreateDatabase(vocbase->name());

  ASSERT_TRUE(ci.createCollectionCoordinator(
                    vocbase->name(), collectionId, 0, 1, 1, false,
                    collectionJson->slice(), 0.0, false, nullptr,
                    darbotdb::replication::Version::ONE)
                  .ok());
  auto logicalCollection = ci.getCollection(vocbase->name(), collectionId);
  ASSERT_FALSE(!logicalCollection);
  EXPECT_TRUE(
      (ci.createViewCoordinator(vocbase->name(), viewId, viewJson->slice())
           .ok()));
  auto logicalView0 = ci.getView(vocbase->name(), viewId);
  ASSERT_FALSE(!logicalView0);

  darbotdb::velocypack::Builder tmp;

  auto const currentCollectionPath =
      "/Current/Collections/" + vocbase->name() + "/" +
      std::to_string(logicalCollection->id().id());
  {
    ASSERT_TRUE(logicalView0);
    auto const viewId = std::to_string(logicalView0->planId().id());
    EXPECT_TRUE("42" == viewId);

    // simulate heartbeat thread (create index in current)
    {
      auto const value = darbotdb::velocypack::Parser::fromJson(
          "{ \"s998\": { \"indexes\" : [ { \"id\": \"1\" } ] } }");
      EXPECT_TRUE(darbotdb::AgencyComm(server.server())
                      .setValue(currentCollectionPath, value->slice(), 0.0)
                      .successful());
    }
  }

  auto [t, i] =
      server.getFeature<darbotdb::ClusterFeature>().agencyCache().read(
          std::vector<std::string>{"/arango"});

  ASSERT_TRUE((darbotdb::methods::Indexes::ensureIndex(
                   *logicalCollection, linkJson->slice(), true, tmp)
                   .waitAndGet()
                   .ok()));
  logicalCollection = ci.getCollection(vocbase->name(), collectionId);
  ASSERT_FALSE(!logicalCollection);
  auto link0 = darbotdb::iresearch::IResearchLinkHelper::find(
      *logicalCollection, *logicalView0);
  ASSERT_FALSE(!link0);

  darbotdb::velocypack::Builder builder;
  builder.openObject();
  EXPECT_TRUE(
      logicalView0
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(0,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 0 before upgrade

  // ensure no upgrade on coordinator
  // simulate heartbeat thread (create index in current)
  {
    auto const path = "/Current/Collections/" + vocbase->name() + "/" +
                      std::to_string(logicalCollection->id().id());
    auto const value = darbotdb::velocypack::Parser::fromJson(
        "{ \"s999\": { \"indexes\" : [ { \"id\": \"2\" } "
        "] } }");
    EXPECT_TRUE(darbotdb::AgencyComm(server.server())
                    .setValue(path, value->slice(), 0.0)
                    .successful());

    VPackBuilder b;
    {
      VPackArrayBuilder trxs(&b);
      {
        VPackArrayBuilder trx(&b);
        {
          VPackObjectBuilder op(&b);
          b.add(path, value->slice());
        }
      }
    }
    server.getFeature<darbotdb::ClusterFeature>()
        .agencyCache()
        .applyTestTransaction(b.slice());
  }
  EXPECT_TRUE(darbotdb::methods::Upgrade::clusterBootstrap(*vocbase)
                  .ok());  // run upgrade
  auto logicalCollection2 = ci.getCollection(vocbase->name(), collectionId);
  ASSERT_FALSE(!logicalCollection2);
  auto logicalView1 = ci.getView(vocbase->name(), viewId);
  EXPECT_FALSE(!logicalView1);  // ensure view present after upgrade
  EXPECT_EQ(logicalView0->id(), logicalView1->id());  // ensure same id for view
  auto link1 = darbotdb::iresearch::IResearchLinkHelper::find(
      *logicalCollection2, *logicalView1);
  EXPECT_FALSE(!link1);  // ensure link present after upgrade
  EXPECT_EQ(link0->index().id(), link1->index().id());  // ensure new link
  builder.clear();
  builder.openObject();
  EXPECT_TRUE(
      logicalView1
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(0,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 0 after upgrade
}

class IResearchFeatureTestDBServer
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AGENCY,
                                            darbotdb::LogLevel::FATAL>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::CLUSTER,
                                            darbotdb::LogLevel::FATAL> {
 protected:
  darbotdb::tests::mocks::MockDBServer server;

 private:
 protected:
  IResearchFeatureTestDBServer() : server("PRMR_0001", false) {
    darbotdb::tests::init();

    darbotdb::ServerState::instance()->setRebootId(
        darbotdb::RebootId{1});  // Hack.

    // server.addFeature<darbotdb::SchedulerFeature>(true);
    // we will control UgradeFeature start!
    server.untrackFeature<darbotdb::UpgradeFeature>();
    server.startFeatures();
  }

  // version 0 data-source path
  std::filesystem::path getPersistedPath0(darbotdb::LogicalView const& view) {
    auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
    std::filesystem::path dataPath(dbPathFeature.directory());
    dataPath /= "databases";
    dataPath /= "database-";
    dataPath += std::to_string(view.vocbase().id());
    dataPath /= darbotdb::iresearch::StaticStrings::ViewArangoSearchType;
    dataPath += "-";
    dataPath += std::to_string(view.id().id());
    return dataPath;
  }

  // version 1 data-source path
  std::filesystem::path getPersistedPath1(
      darbotdb::iresearch::IResearchLink const& link) {
    auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
    std::filesystem::path dataPath(dbPathFeature.directory());
    dataPath /= "databases";
    dataPath /= "database-";
    dataPath += std::to_string(link.index().collection().vocbase().id());
    dataPath /= darbotdb::iresearch::StaticStrings::ViewArangoSearchType;
    dataPath += "-";
    dataPath += std::to_string(link.index().collection().id().id());
    dataPath += "_";
    dataPath += std::to_string(link.index().id().id());
    return dataPath;
  }

  void createTestDatabase(TRI_vocbase_t*& vocbase,
                          std::string const name = "testDatabase") {
    vocbase = server.createDatabase(name);
    ASSERT_NE(nullptr, vocbase);
    ASSERT_EQ(name, vocbase->name());
  }
};

TEST_F(IResearchFeatureTestDBServer, test_upgrade0_1_no_directory) {
  // test db-server (no directory)
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testCollection\" }");
  auto linkJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"view\": \"testView\", \"type\": \"arangosearch\", "
      "\"includeAllFields\": true }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testView\", \"type\": \"arangosearch\", \"version\": 0 "
      "}");
  auto versionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"version\": 0, \"tasks\": {} }");

  server.getFeature<darbotdb::DatabaseFeature>()
      .enableUpgrade();  // skip IResearchView validation

  auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
  darbotdb::tests::setDatabasePath(
      dbPathFeature);  // ensure test data is stored in a unique directory
  auto versionFilename = StorageEngineMock::versionFilenameResult;
  irs::Finally versionFilenameRestore = [&versionFilename]() noexcept {
    StorageEngineMock::versionFilenameResult = versionFilename;
  };
  StorageEngineMock::versionFilenameResult =
      (std::filesystem::path(dbPathFeature.directory()) /= "version").string();
  ASSERT_TRUE(irs::file_utils::mkdir(
      std::filesystem::path(dbPathFeature.directory()).c_str(), true));
  ASSERT_TRUE((darbotdb::basics::VelocyPackHelper::velocyPackToFile(
      StorageEngineMock::versionFilenameResult, versionJson->slice(), false)));

  VPackBuilder bogus;
  {
    VPackArrayBuilder trxs(&bogus);
    {
      VPackArrayBuilder trx(&bogus);
      {
        VPackObjectBuilder op(&bogus);
        bogus.add("a", VPackValue(12));
      }
    }
  }
  server.server()
      .getFeature<darbotdb::ClusterFeature>()
      .agencyCache()
      .applyTestTransaction(bogus.slice());

  TRI_vocbase_t vocbase(testDBInfo(server.server()));
  auto logicalCollection = vocbase.createCollection(collectionJson->slice());
  ASSERT_FALSE(!logicalCollection);
  auto logicalView = vocbase.createView(viewJson->slice(), false);
  ASSERT_FALSE(!logicalView);
  auto* view =
      dynamic_cast<darbotdb::iresearch::IResearchView*>(logicalView.get());
  ASSERT_FALSE(!view);
  bool created = false;
  auto index =
      logicalCollection->createIndex(linkJson->slice(), created).waitAndGet();
  ASSERT_TRUE(created);
  ASSERT_FALSE(!index);
  auto link =
      std::dynamic_pointer_cast<darbotdb::iresearch::IResearchLink>(index);
  ASSERT_FALSE(!link);
  ASSERT_TRUE(
      view->link(link->self()).ok());  // link will not notify view in
                                       // 'vocbase', hence notify manually

  index->unload();  // release file handles
  bool result;
  auto linkDataPath = getPersistedPath1(*link);
  EXPECT_TRUE(
      irs::file_utils::remove(linkDataPath.c_str()));  // remove link directory
  auto viewDataPath = getPersistedPath0(*logicalView);
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure no view directory
  darbotdb::velocypack::Builder builder;
  builder.openObject();
  EXPECT_TRUE(
      logicalView
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(0,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 0 before upgrade

  EXPECT_TRUE(darbotdb::methods::Upgrade::startup(vocbase, true, false)
                  .ok());  // run upgrade
  logicalView = vocbase.lookupView(logicalView->name());
  EXPECT_FALSE(logicalView);  // ensure view removed after upgrade
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure view directory not present
}

TEST_F(IResearchFeatureTestDBServer, test_upgrade0_1_with_directory) {
  // test db-server (with directory)
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testCollection\" }");
  auto linkJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"view\": \"testView\", \"type\": \"arangosearch\", "
      "\"includeAllFields\": true }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testView\", \"type\": \"arangosearch\", \"version\": 0 "
      "}");
  auto versionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"version\": 0, \"tasks\": {} }");

  server.getFeature<darbotdb::DatabaseFeature>()
      .enableUpgrade();  // skip IResearchView validation

  auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
  darbotdb::tests::setDatabasePath(
      dbPathFeature);  // ensure test data is stored in a unique directory
  auto versionFilename = StorageEngineMock::versionFilenameResult;
  irs::Finally versionFilenameRestore = [&versionFilename]() noexcept {
    StorageEngineMock::versionFilenameResult = versionFilename;
  };
  StorageEngineMock::versionFilenameResult =
      (std::filesystem::path(dbPathFeature.directory()) /= "version").string();
  ASSERT_TRUE(irs::file_utils::mkdir(
      std::filesystem::path(dbPathFeature.directory()).c_str(), true));
  ASSERT_TRUE((darbotdb::basics::VelocyPackHelper::velocyPackToFile(
      StorageEngineMock::versionFilenameResult, versionJson->slice(), false)));

  auto& engine = *static_cast<StorageEngineMock*>(
      &server.getFeature<darbotdb::EngineSelectorFeature>().engine());
  engine.views.clear();

  VPackBuilder bogus;
  {
    VPackArrayBuilder trxs(&bogus);
    {
      VPackArrayBuilder trx(&bogus);
      {
        VPackObjectBuilder op(&bogus);
        bogus.add("a", VPackValue(12));
      }
    }
  }
  server.server()
      .getFeature<darbotdb::ClusterFeature>()
      .agencyCache()
      .applyTestTransaction(bogus.slice());

  TRI_vocbase_t vocbase(testDBInfo(server.server()));
  auto logicalCollection = vocbase.createCollection(collectionJson->slice());
  ASSERT_FALSE(!logicalCollection);
  auto logicalView = vocbase.createView(viewJson->slice(), false);
  ASSERT_FALSE(!logicalView);
  auto* view =
      dynamic_cast<darbotdb::iresearch::IResearchView*>(logicalView.get());
  ASSERT_FALSE(!view);
  bool created;
  auto index =
      logicalCollection->createIndex(linkJson->slice(), created).waitAndGet();
  ASSERT_TRUE(created);
  ASSERT_FALSE(!index);
  auto link =
      std::dynamic_pointer_cast<darbotdb::iresearch::IResearchLink>(index);
  ASSERT_FALSE(!link);
  ASSERT_TRUE(
      view->link(link->self()).ok());  // link will not notify view in
                                       // 'vocbase', hence notify manually

  index->unload();  // release file handles
  bool result;
  auto linkDataPath = getPersistedPath1(*link);
  EXPECT_TRUE(
      irs::file_utils::remove(linkDataPath.c_str()));  // remove link directory
  auto viewDataPath = getPersistedPath0(*logicalView);
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) && !result);
  EXPECT_TRUE(irs::file_utils::mkdir(viewDataPath.c_str(),
                                     true));  // create view directory
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) && result);
  darbotdb::velocypack::Builder builder;
  builder.openObject();
  EXPECT_TRUE(
      logicalView
          ->properties(builder,
                       darbotdb::LogicalDataSource::Serialization::Persistence)
          .ok());
  builder.close();
  EXPECT_EQ(0,
            builder.slice()
                .get("version")
                .getNumber<uint32_t>());  // ensure 'version == 0 before upgrade

  EXPECT_TRUE(darbotdb::methods::Upgrade::startup(vocbase, true, false)
                  .ok());  // run upgrade
  //    EXPECT_TRUE(darbotdb::methods::Upgrade::clusterBootstrap(vocbase).ok());
  //    // run upgrade
  logicalView = vocbase.lookupView(logicalView->name());
  EXPECT_FALSE(logicalView);  // ensure view removed after upgrade
  EXPECT_TRUE(irs::file_utils::exists(result, viewDataPath.c_str()) &&
              !result);  // ensure view directory removed after upgrade
}

TEST_F(IResearchFeatureTestDBServer, test_upgrade1_link_collectionName) {
  // test db-server (with directory)
  // auto collectionJson = darbotdb::velocypack::Parser::fromJson(
  //    "{ \"name\": \"testCollection\", \"id\":999 }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testView\", \"type\": \"arangosearch\", \"version\": 1 "
      "}");

  auto linkJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"view\": \"testView\", \"type\": \"arangosearch\", "
      "\"includeAllFields\": true }");
  // assume step 1 already finished
  auto versionJson = darbotdb::velocypack::Parser::fromJson(
      std::string("{ \"version\": ") +
      std::to_string(darbotdb::methods::Version::current()) +
      ", \"tasks\": {\"upgradeArangoSearch0_1\":true} }");

  server.getFeature<darbotdb::DatabaseFeature>()
      .enableUpgrade();  // skip IResearchView validation

  auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
  darbotdb::tests::setDatabasePath(
      dbPathFeature);  // ensure test data is stored in a unique directory
  auto versionFilename = StorageEngineMock::versionFilenameResult;
  irs::Finally versionFilenameRestore = [&versionFilename]() noexcept {
    StorageEngineMock::versionFilenameResult = versionFilename;
  };
  StorageEngineMock::versionFilenameResult =
      (std::filesystem::path(dbPathFeature.directory()) /= "version").string();
  ASSERT_TRUE(irs::file_utils::mkdir(
      std::filesystem::path(dbPathFeature.directory()).c_str(), true));

  auto& engine = *static_cast<StorageEngineMock*>(
      &server.getFeature<darbotdb::EngineSelectorFeature>().engine());
  engine.views.clear();

  TRI_vocbase_t* vocbase;
  createTestDatabase(vocbase);

  // rewrite file so upgrade task was not executed
  ASSERT_TRUE((darbotdb::basics::VelocyPackHelper::velocyPackToFile(
      StorageEngineMock::versionFilenameResult, versionJson->slice(), false)));

  auto& clusterInfo =
      vocbase->server().getFeature<darbotdb::ClusterFeature>().clusterInfo();

  // server.createCollection("testDatabase", "999", "testCollection");
  auto logicalCollectionCluster = clusterInfo.getCollection(
      "testDatabase",
      "_analyzers");  // createCollection(collectionJson->slice());
  ASSERT_FALSE(!logicalCollectionCluster);

  // now we have standart collections in ClusterInfo
  // we need corresponding collection in vocbase with the same id!
  // FIXME: remove this as soon as proper DBServer mock will be ready
  // and  createTestDatabase will actually fill collections in vocbase
  std::string collectionJson =
      "{ \"isSystem\":true, \"name\": \"_analyzers\", \"id\":";
  collectionJson.append(std::to_string(logicalCollectionCluster->id().id()))
      .append("}");
  auto logicalCollection =
      vocbase->createCollection(VPackParser::fromJson(collectionJson)->slice());

  auto logicalView = vocbase->createView(viewJson->slice(), false);
  ASSERT_FALSE(!logicalView);
  auto* view =
      dynamic_cast<darbotdb::iresearch::IResearchView*>(logicalView.get());
  bool created;
  auto index =
      logicalCollection->createIndex(linkJson->slice(), created).waitAndGet();
  ASSERT_TRUE(created);
  ASSERT_FALSE(!index);
  auto link =
      std::dynamic_pointer_cast<darbotdb::iresearch::IResearchLink>(index);
  ASSERT_FALSE(!link);
  ASSERT_TRUE(
      view->link(link->self()).ok());  // link will not notify view in
                                       // 'vocbase', hence notify manually

  {
    auto indexes = logicalCollection->getPhysical()->getReadyIndexes();
    for (auto& index : indexes) {
      if (index->type() ==
          darbotdb::Index::IndexType::TRI_IDX_TYPE_IRESEARCH_LINK) {
        VPackBuilder builder;
        index->toVelocyPack(
            builder,
            darbotdb::Index::makeFlags(darbotdb::Index::Serialize::Internals));
        ASSERT_FALSE(builder.slice().hasKey("collectionName"));
      }
    }
  }

  EXPECT_TRUE(darbotdb::methods::Upgrade::startup(*vocbase, false, false)
                  .ok());  // run upgrade

  {
    auto indexes = logicalCollection->getPhysical()->getReadyIndexes();
    for (auto& index : indexes) {
      if (index->type() ==
          darbotdb::Index::IndexType::TRI_IDX_TYPE_IRESEARCH_LINK) {
        VPackBuilder builder;
        index->toVelocyPack(
            builder,
            darbotdb::Index::makeFlags(darbotdb::Index::Serialize::Internals));
        auto slice = builder.slice();
        ASSERT_TRUE(slice.hasKey("collectionName"));
        ASSERT_EQ("_analyzers", slice.get("collectionName").copyString());
      }
    }
  }
}
