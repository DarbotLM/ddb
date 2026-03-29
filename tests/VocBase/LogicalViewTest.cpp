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

#include <velocypack/Parser.h>

#include "IResearch/common.h"
#include "Mocks/LogLevels.h"
#include "Mocks/StorageEngineMock.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "Aql/QueryRegistry.h"
#include "GeneralServer/AuthenticationFeature.h"
#include "Logger/Logger.h"
#include "RestServer/DatabaseFeature.h"
#include "Metrics/MetricsFeature.h"
#include "RestServer/QueryRegistryFeature.h"
#include "RestServer/ViewTypesFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "Utils/ExecContext.h"
#include "VocBase/LogicalView.h"
#include "VocBase/VocbaseInfo.h"
#include "VocBase/vocbase.h"
#include "Cluster/ClusterFeature.h"
#include "Metrics/ClusterMetricsFeature.h"
#include "Statistics/StatisticsFeature.h"
#include "RestServer/QueryRegistryFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"

namespace {
struct TestView : public darbotdb::LogicalView {
  static constexpr auto typeInfo() noexcept {
    return std::pair{static_cast<darbotdb::ViewType>(42),
                     std::string_view{"testViewType"}};
  }

  darbotdb::Result _appendVelocyPackResult;
  darbotdb::velocypack::Builder _properties;

  TestView(TRI_vocbase_t& vocbase,
           darbotdb::velocypack::Slice const& definition)
      : darbotdb::LogicalView(*this, vocbase, definition, false) {}
  darbotdb::Result appendVPackImpl(darbotdb::velocypack::Builder& build,
                                   Serialization, bool) const override {
    build.add("properties", _properties.slice());
    return _appendVelocyPackResult;
  }
  virtual darbotdb::Result dropImpl() override {
    return darbotdb::storage_helper::drop(*this);
  }
  virtual void open() override {}
  virtual darbotdb::Result renameImpl(std::string const& oldName) override {
    return darbotdb::storage_helper::rename(*this, oldName);
  }
  virtual darbotdb::Result properties(darbotdb::velocypack::Slice properties,
                                      bool /*isUserRequest*/,
                                      bool /*partialUpdate*/) override {
    _properties = darbotdb::velocypack::Builder(properties);
    return darbotdb::Result();
  }
  virtual bool visitCollections(
      CollectionVisitor const& /*visitor*/) const override {
    return true;
  }
};

struct ViewFactory : public darbotdb::ViewFactory {
  virtual darbotdb::Result create(darbotdb::LogicalView::ptr& view,
                                  TRI_vocbase_t& vocbase,
                                  darbotdb::velocypack::Slice definition,
                                  bool isUserRequest) const override {
    view = vocbase.createView(definition, isUserRequest);

    return darbotdb::Result();
  }

  virtual darbotdb::Result instantiate(darbotdb::LogicalView::ptr& view,
                                       TRI_vocbase_t& vocbase,
                                       darbotdb::velocypack::Slice definition,
                                       bool /*isUserRequest*/) const override {
    view = std::make_shared<TestView>(vocbase, definition);

    return darbotdb::Result();
  }
};

}  // namespace

class LogicalViewTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 protected:
  darbotdb::ArangodServer server;
  StorageEngineMock engine;
  std::vector<
      std::pair<darbotdb::application_features::ApplicationFeature&, bool>>
      features;
  ViewFactory viewFactory;

  LogicalViewTest() : server(nullptr, nullptr), engine(server) {
    auto& selector = server.addFeature<darbotdb::EngineSelectorFeature>();
    features.emplace_back(selector, false);
    selector.setEngineTesting(&engine);
    features.emplace_back(server.addFeature<darbotdb::AuthenticationFeature>(),
                          false);  // required for ExecContext
    features.emplace_back(server.addFeature<darbotdb::DatabaseFeature>(),
                          false);
    features.emplace_back(
        server.addFeature<darbotdb::metrics::MetricsFeature>(
            darbotdb::LazyApplicationFeatureReference<
                darbotdb::QueryRegistryFeature>(server),
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
    features.emplace_back(server.addFeature<darbotdb::ViewTypesFeature>(),
                          false);  // required for LogicalView::create(...)

    for (auto& f : features) {
      f.first.prepare();
    }

    for (auto& f : features) {
      if (f.second) {
        f.first.start();
      }
    }

    auto& viewTypesFeature = server.getFeature<darbotdb::ViewTypesFeature>();
    viewTypesFeature.emplace(TestView::typeInfo().second, viewFactory);
  }

  ~LogicalViewTest() {
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

TEST_F(LogicalViewTest, test_auth) {
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\": \"testView\", \"type\": \"testViewType\" }");

  // no ExecContext
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto logicalView = vocbase.createView(viewJson->slice(), false);
    EXPECT_TRUE(logicalView->canUse(darbotdb::auth::Level::RW));
  }

  // no read access
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto logicalView = vocbase.createView(viewJson->slice(), false);
    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "",
                                  "testVocbase", darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    EXPECT_FALSE(logicalView->canUse(darbotdb::auth::Level::RO));
  }

  // no write access
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto logicalView = vocbase.createView(viewJson->slice(), false);
    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "",
                                  "testVocbase", darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::RO, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    EXPECT_TRUE(logicalView->canUse(darbotdb::auth::Level::RO));
    EXPECT_FALSE(logicalView->canUse(darbotdb::auth::Level::RW));
  }

  // write access (view access is db access as per
  // https://github.com/darbotdb/backlog/issues/459)
  {
    TRI_vocbase_t vocbase(testDBInfo(server));
    auto logicalView = vocbase.createView(viewJson->slice(), false);
    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "",
                                  "testVocbase", darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::RW, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    EXPECT_TRUE(logicalView->canUse(darbotdb::auth::Level::RO));
    EXPECT_TRUE(logicalView->canUse(darbotdb::auth::Level::RW));
  }
}
