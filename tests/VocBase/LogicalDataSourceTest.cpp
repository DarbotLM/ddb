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

#include "../IResearch/common.h"
#include "../Mocks/StorageEngineMock.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "RestServer/DatabaseFeature.h"
#include "Metrics/MetricsFeature.h"
#include "RestServer/QueryRegistryFeature.h"
#include "Sharding/ShardingFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/LogicalView.h"
#include "velocypack/Parser.h"

namespace {

class LogicalViewImpl : public darbotdb::LogicalView {
 public:
  static constexpr auto typeInfo() noexcept {
    return std::pair{static_cast<darbotdb::ViewType>(52),
                     std::string_view{"LogicalViewImpl"}};
  }

  LogicalViewImpl(TRI_vocbase_t& vocbase,
                  darbotdb::velocypack::Slice const& definition)
      : LogicalView(*this, vocbase, definition, false) {}
  darbotdb::Result appendVPackImpl(darbotdb::velocypack::Builder&,
                                   Serialization, bool) const override {
    return {};
  }
  virtual darbotdb::Result dropImpl() override { return darbotdb::Result(); }
  virtual void open() override {}
  virtual darbotdb::Result renameImpl(std::string const&) override {
    return darbotdb::Result();
  }
  virtual darbotdb::Result properties(
      darbotdb::velocypack::Slice /*properties*/, bool /*isUserRequest*/,
      bool /*partialUpdate*/) override {
    return darbotdb::Result();
  }
  virtual bool visitCollections(
      CollectionVisitor const& /*visitor*/) const override {
    return true;
  }
};

}  // namespace

class LogicalDataSourceTest : public ::testing::Test {
 protected:
  darbotdb::ArangodServer server;
  StorageEngineMock engine;
  std::vector<
      std::pair<darbotdb::application_features::ApplicationFeature&, bool>>
      features;

  LogicalDataSourceTest() : server(nullptr, nullptr), engine(server) {
    // setup required application features
    auto& selector = server.addFeature<darbotdb::EngineSelectorFeature>();
    features.emplace_back(selector, false);
    selector.setEngineTesting(&engine);
    features.emplace_back(server.addFeature<darbotdb::DatabaseFeature>(),
                          false);
    features.emplace_back(
        server.addFeature<darbotdb::metrics::MetricsFeature>(
            darbotdb::LazyApplicationFeatureReference<
                darbotdb::QueryRegistryFeature>(nullptr),
            darbotdb::LazyApplicationFeatureReference<
                darbotdb::StatisticsFeature>(nullptr),
            darbotdb::LazyApplicationFeatureReference<
                darbotdb::EngineSelectorFeature>(nullptr),
            darbotdb::LazyApplicationFeatureReference<
                darbotdb::metrics::ClusterMetricsFeature>(nullptr),
            darbotdb::LazyApplicationFeatureReference<darbotdb::ClusterFeature>(
                nullptr)),
        false);
    features.emplace_back(
        server.addFeature<darbotdb::QueryRegistryFeature>(
            server.template getFeature<darbotdb::metrics::MetricsFeature>()),
        false);  // required for TRI_vocbase_t
    features.emplace_back(server.addFeature<darbotdb::ShardingFeature>(),
                          false);

    for (auto& f : features) {
      f.first.prepare();
    }

    for (auto& f : features) {
      if (f.second) {
        f.first.start();
      }
    }
  }

  ~LogicalDataSourceTest() {
    server.getFeature<darbotdb::EngineSelectorFeature>().setEngineTesting(
        nullptr);

    // destroy application features
    for (auto& f : features) {
      if (f.second) {
        f.first.stop();
      }
    }

    for (auto& f : features) {
      f.first.unprepare();
    }
  }
};

// -----------------------------------------------------------------------------
// --SECTION--                                                        test suite
// -----------------------------------------------------------------------------

TEST_F(LogicalDataSourceTest, test_category) {
  // LogicalCollection
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto json = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testCollection\" }");
    darbotdb::LogicalCollection instance(vocbase, json->slice(), true);

    EXPECT_EQ(darbotdb::LogicalDataSource::Category::kCollection,
              instance.category());
  }

  // LogicalView
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto json =
        darbotdb::velocypack::Parser::fromJson("{ \"name\": \"testView\" }");
    LogicalViewImpl instance(vocbase, json->slice());

    EXPECT_EQ(darbotdb::LogicalDataSource::Category::kView,
              instance.category());
  }
}

TEST_F(LogicalDataSourceTest, test_construct) {
  // LogicalCollection
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto json = darbotdb::velocypack::Parser::fromJson(
        "{ \"id\": 1, \"planId\": 2, \"globallyUniqueId\": \"abc\", \"name\": "
        "\"testCollection\" }");
    darbotdb::LogicalCollection instance(vocbase, json->slice(), true);

    EXPECT_EQ(1, instance.id().id());
    EXPECT_EQ(2, instance.planId().id());
    EXPECT_EQ(std::string("abc"), instance.guid());
  }

  // LogicalView
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto json = darbotdb::velocypack::Parser::fromJson(
        "{ \"id\": 1, \"planId\": 2, \"globallyUniqueId\": \"abc\", \"name\": "
        "\"testView\" }");
    LogicalViewImpl instance(vocbase, json->slice());

    EXPECT_EQ(1, instance.id().id());
    EXPECT_EQ(2, instance.planId().id());
    EXPECT_EQ(std::string("abc"), instance.guid());
  }
}

TEST_F(LogicalDataSourceTest, test_defaults) {
  // LogicalCollection
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto json = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testCollection\" }");
    darbotdb::LogicalCollection instance(vocbase, json->slice(), true);

    EXPECT_TRUE(instance.id().isSet());
    EXPECT_TRUE(instance.planId().isSet());
    EXPECT_FALSE(instance.guid().empty());
  }

  // LogicalView
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto json =
        darbotdb::velocypack::Parser::fromJson("{ \"name\": \"testView\" }");
    LogicalViewImpl instance(vocbase, json->slice());

    EXPECT_TRUE(instance.id().isSet());
    EXPECT_TRUE(instance.planId().isSet());
    EXPECT_EQ(instance.id(), instance.planId());
    EXPECT_FALSE(instance.guid().empty());
  }
}
