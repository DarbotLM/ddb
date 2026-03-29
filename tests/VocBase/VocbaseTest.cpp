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
#include "Mocks/Servers.h"

#include "RestServer/DatabaseFeature.h"
#include "RestServer/QueryRegistryFeature.h"
#include "RestServer/ViewTypesFeature.h"
#include "Sharding/ShardingFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/LogicalView.h"
#include "velocypack/Parser.h"

#include <memory>

namespace {
struct TestView : public darbotdb::LogicalView {
  static constexpr auto typeInfo() noexcept {
    return std::pair{static_cast<darbotdb::ViewType>(42),
                     std::string_view{"testViewType"}};
  }

  TestView(TRI_vocbase_t& vocbase,
           darbotdb::velocypack::Slice const& definition)
      : darbotdb::LogicalView(*this, vocbase, definition, false) {}
  darbotdb::Result appendVPackImpl(darbotdb::velocypack::Builder&,
                                   Serialization, bool) const override {
    return {};
  }
  virtual darbotdb::Result dropImpl() override {
    return darbotdb::storage_helper::drop(*this);
  }
  virtual void open() override {}
  virtual darbotdb::Result renameImpl(std::string const& oldName) override {
    return darbotdb::storage_helper::rename(*this, oldName);
  }
  virtual darbotdb::Result properties(darbotdb::velocypack::Slice, bool,
                                      bool) override {
    return darbotdb::Result();
  }
  virtual bool visitCollections(CollectionVisitor const&) const override {
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

class VocbaseTest : public ::testing::Test {
 protected:
  darbotdb::tests::mocks::MockAqlServer server;
  ViewFactory viewFactory;

  VocbaseTest() {
    // register view factory
    server.getFeature<darbotdb::ViewTypesFeature>().emplace(
        TestView::typeInfo().second, viewFactory);
  }
};

TEST_F(VocbaseTest, test_lookupDataSource) {
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"globallyUniqueId\": \"testCollectionGUID\", \"id\": 100, \"name\": "
      "\"testCollection\" }");
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"id\": 200, \"name\": \"testView\", \"type\": \"testViewType\" "
      "}");  // any arbitrary view type
  Vocbase vocbase(testDBInfo(server.server()));

  // not present collection (no datasource)
  {
    EXPECT_FALSE(vocbase.lookupDataSource(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(vocbase.lookupDataSource("100"));
    EXPECT_FALSE(vocbase.lookupDataSource("testCollection"));
    EXPECT_FALSE(vocbase.lookupDataSource("testCollectionGUID"));
    EXPECT_FALSE(vocbase.lookupCollection(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(vocbase.lookupCollection("100"));
    EXPECT_FALSE(vocbase.lookupCollection("testCollection"));
    EXPECT_FALSE(vocbase.lookupCollection("testCollectionGUID"));
  }

  // not present view (no datasource)
  {
    EXPECT_FALSE(vocbase.lookupDataSource(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(vocbase.lookupDataSource("200"));
    EXPECT_FALSE(vocbase.lookupDataSource("testView"));
    EXPECT_FALSE(vocbase.lookupDataSource("testViewGUID"));
    EXPECT_FALSE(vocbase.lookupView(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(vocbase.lookupView("200"));
    EXPECT_FALSE(vocbase.lookupView("testView"));
    EXPECT_FALSE(vocbase.lookupView("testViewGUID"));
  }

  auto collection = vocbase.createCollection(collectionJson->slice());
  auto view = vocbase.createView(viewJson->slice(), false);

  EXPECT_FALSE(collection->deleted());
  EXPECT_FALSE(view->deleted());

  // not present collection (is view)
  {
    EXPECT_FALSE(!vocbase.lookupDataSource(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(!vocbase.lookupDataSource("200"));
    EXPECT_FALSE(!vocbase.lookupDataSource("testView"));
    EXPECT_FALSE(vocbase.lookupDataSource("testViewGUID"));
    EXPECT_FALSE(vocbase.lookupCollection(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(vocbase.lookupCollection("200"));
    EXPECT_FALSE(vocbase.lookupCollection("testView"));
    EXPECT_FALSE(vocbase.lookupCollection("testViewGUID"));
    EXPECT_FALSE(vocbase.lookupCollectionByUuid("testView"));
    EXPECT_FALSE(vocbase.lookupCollectionByUuid("testViewGUID"));
  }

  // not preset view (is collection)
  {
    EXPECT_FALSE(!vocbase.lookupDataSource(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(!vocbase.lookupDataSource("100"));
    EXPECT_FALSE(!vocbase.lookupDataSource("testCollection"));
    EXPECT_FALSE(!vocbase.lookupDataSource("testCollectionGUID"));
    EXPECT_FALSE(vocbase.lookupView(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(vocbase.lookupView("100"));
    EXPECT_FALSE(vocbase.lookupView("testCollection"));
    EXPECT_FALSE(vocbase.lookupView("testCollectionGUID"));
  }

  // present collection
  {
    EXPECT_FALSE(!vocbase.lookupDataSource(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(!vocbase.lookupDataSource("100"));
    EXPECT_FALSE(!vocbase.lookupDataSource("testCollection"));
    EXPECT_FALSE(!vocbase.lookupDataSource("testCollectionGUID"));
    EXPECT_FALSE(!vocbase.lookupCollection(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(!vocbase.lookupCollection("100"));
    EXPECT_FALSE(!vocbase.lookupCollection("testCollection"));
    EXPECT_FALSE(!vocbase.lookupCollection("testCollectionGUID"));
    EXPECT_FALSE(vocbase.lookupCollectionByUuid("testCollection"));
    EXPECT_TRUE(
        (false == !vocbase.lookupCollectionByUuid("testCollectionGUID")));
  }

  // present view
  {
    EXPECT_FALSE(!vocbase.lookupDataSource(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(!vocbase.lookupDataSource("200"));
    EXPECT_FALSE(!vocbase.lookupDataSource("testView"));
    EXPECT_FALSE(vocbase.lookupDataSource("testViewGUID"));
    EXPECT_FALSE(!vocbase.lookupView(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(!vocbase.lookupView("200"));
    EXPECT_FALSE(!vocbase.lookupView("testView"));
    EXPECT_FALSE(vocbase.lookupView("testViewGUID"));
  }

  EXPECT_TRUE(vocbase.dropCollection(collection->id(), true).ok());
  EXPECT_TRUE(view->drop().ok());
  EXPECT_TRUE(collection->deleted());
  EXPECT_TRUE(view->deleted());

  // not present collection (deleted)
  {
    EXPECT_FALSE(vocbase.lookupDataSource(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(vocbase.lookupDataSource("100"));
    EXPECT_FALSE(vocbase.lookupDataSource("testCollection"));
    EXPECT_FALSE(vocbase.lookupDataSource("testCollectionGUID"));
    EXPECT_FALSE(vocbase.lookupCollection(darbotdb::DataSourceId{100}));
    EXPECT_FALSE(vocbase.lookupCollection("100"));
    EXPECT_FALSE(vocbase.lookupCollection("testCollection"));
    EXPECT_FALSE(vocbase.lookupCollection("testCollectionGUID"));
    EXPECT_FALSE(vocbase.lookupCollectionByUuid("testCollection"));
    EXPECT_TRUE(
        (true == !vocbase.lookupCollectionByUuid("testCollectionGUID")));
  }

  // not present view (deleted)
  {
    EXPECT_FALSE(vocbase.lookupDataSource(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(vocbase.lookupDataSource("200"));
    EXPECT_FALSE(vocbase.lookupDataSource("testView"));
    EXPECT_FALSE(vocbase.lookupDataSource("testViewGUID"));
    EXPECT_FALSE(vocbase.lookupView(darbotdb::DataSourceId{200}));
    EXPECT_FALSE(vocbase.lookupView("200"));
    EXPECT_FALSE(vocbase.lookupView("testView"));
    EXPECT_FALSE(vocbase.lookupView("testViewGUID"));
    EXPECT_FALSE(vocbase.lookupCollectionByUuid("testCollection"));
    EXPECT_TRUE(
        (true == !vocbase.lookupCollectionByUuid("testCollectionGUID")));
  }
}
