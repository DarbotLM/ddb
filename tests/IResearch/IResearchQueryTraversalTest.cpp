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

#include <absl/strings/str_replace.h>

#include <velocypack/Iterator.h>

#include "IResearch/IResearchVPackComparer.h"
#include "IResearch/IResearchView.h"
#include "IResearch/IResearchViewSort.h"
#include "IResearchQueryCommon.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/OperationOptions.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"
#include "store/mmap_directory.hpp"
#include "utils/index_utils.hpp"

namespace darbotdb::tests {
namespace {

class QueryTraversal : public QueryTest {
 protected:
  void create() {
    // create _graphs system collection as it is required to parse graph queries
    {
      auto createJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"_graphs\", \"isSystem\": true }");
      auto collection = _vocbase.createCollection(createJson->slice());
      ASSERT_NE(nullptr, collection);
    }
    // create collection0
    {
      auto createJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"testCollection0\" }");
      auto collection = _vocbase.createCollection(createJson->slice());
      ASSERT_NE(nullptr, collection);

      std::vector<std::shared_ptr<darbotdb::velocypack::Builder>> docs{
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/0\", \"_key\": \"0\", \"seq\": -6, "
              "\"value\": null }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/1\", \"_key\": \"1\", \"seq\": -5, "
              "\"value\": true }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/2\", \"_key\": \"2\", \"seq\": -4, "
              "\"value\": \"abc\" }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/3\", \"_key\": \"3\", \"seq\": -3, "
              "\"value\": 3.14 }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/4\", \"_key\": \"4\", \"seq\": -2, "
              "\"value\": [ 1, \"abc\" ] }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/5\", \"_key\": \"5\", \"seq\": -1, "
              "\"value\": { \"a\": 7, \"b\": \"c\" } }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_id\": \"testCollection0/6\", \"_key\": \"6\", \"seq\": 0, "
              "\"value\": { \"a\": 7, \"b\": \"c\" } }"),
      };

      darbotdb::OperationOptions options;
      options.returnNew = true;
      darbotdb::SingleCollectionTransaction trx(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          *collection, darbotdb::AccessMode::Type::WRITE);
      EXPECT_TRUE(trx.begin().ok());

      for (auto& entry : docs) {
        auto res = trx.insert(collection->name(), entry->slice(), options);
        EXPECT_TRUE(res.ok());
        _insertedDocs.emplace_back(res.slice().get("new"));
      }

      EXPECT_TRUE(trx.commit().ok());
    }
    // create collection1
    {
      auto createJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"testCollection1\" }");
      auto collection = _vocbase.createCollection(createJson->slice());
      ASSERT_NE(nullptr, collection);

      std::filesystem::path resource;
      resource /= std::string_view(darbotdb::tests::testResourceDir);
      resource /= std::string_view("simple_sequential.json");

      auto builder = darbotdb::basics::VelocyPackHelper::velocyPackFromFile(
          resource.string());
      auto slice = builder.slice();
      ASSERT_TRUE(slice.isArray());

      darbotdb::OperationOptions options;
      options.returnNew = true;
      darbotdb::SingleCollectionTransaction trx(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          *collection, darbotdb::AccessMode::Type::WRITE);
      EXPECT_TRUE(trx.begin().ok());

      for (darbotdb::velocypack::ArrayIterator itr(slice); itr.valid(); ++itr) {
        auto res = trx.insert(collection->name(), itr.value(), options);
        EXPECT_TRUE(res.ok());
        _insertedDocs.emplace_back(res.slice().get("new"));
      }

      EXPECT_TRUE(trx.commit().ok());
    }
    // create edge collection
    {
      auto createJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"edges\", \"type\": 3 }");
      auto collection = _vocbase.createCollection(createJson->slice());
      ASSERT_NE(nullptr, collection);

      auto createIndexJson =
          darbotdb::velocypack::Parser::fromJson("{ \"type\": \"edge\" }");
      bool created = false;
      auto index = collection->createIndex(createIndexJson->slice(), created)
                       .waitAndGet();
      EXPECT_TRUE(index);
      EXPECT_TRUE(created);

      darbotdb::SingleCollectionTransaction trx(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          *collection, darbotdb::AccessMode::Type::WRITE);
      EXPECT_TRUE(trx.begin().ok());

      std::vector<std::shared_ptr<darbotdb::velocypack::Builder>> docs{
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_from\": \"testCollection0/0\", \"_to\": "
              "\"testCollection0/1\" }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_from\": \"testCollection0/0\", \"_to\": "
              "\"testCollection0/2\" }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_from\": \"testCollection0/0\", \"_to\": "
              "\"testCollection0/3\" }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_from\": \"testCollection0/0\", \"_to\": "
              "\"testCollection0/4\" }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_from\": \"testCollection0/0\", \"_to\": "
              "\"testCollection0/5\" }"),
          darbotdb::velocypack::Parser::fromJson(
              "{ \"_from\": \"testCollection0/6\", \"_to\": "
              "\"testCollection0/0\" }"),
      };

      darbotdb::OperationOptions options;
      options.returnNew = true;

      for (auto& entry : docs) {
        auto res = trx.insert(collection->name(), entry->slice(), options);
        EXPECT_TRUE(res.ok());
        _insertedDocs.emplace_back(res.slice().get("new"));
      }

      EXPECT_TRUE(trx.commit().ok());
    }
  }

  void queryTests() {
    // check system attribute _from
    {
      std::vector<darbotdb::velocypack::Slice> expectedDocs{
          _insertedDocs.back().slice()};

      auto result =
          darbotdb::tests::executeQuery(_vocbase,
                                        "FOR d IN testViewEdge SEARCH d._from "
                                        "== 'testCollection0/6' RETURN d");
      ASSERT_TRUE(result.result.ok());
      auto slice = result.data->slice();
      EXPECT_TRUE(slice.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(slice);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.begin();
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualDoc = resultIt.value();
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE((0 == darbotdb::basics::VelocyPackHelper::compare(
                              darbotdb::velocypack::Slice(*expectedDoc),
                              resolved, true)));
        ++expectedDoc;
      }
      EXPECT_FALSE(resultIt.valid());
      EXPECT_EQ(expectedDoc, expectedDocs.end());
    }
    // check system attribute _to
    {
      std::vector<darbotdb::velocypack::Slice> expectedDocs{
          _insertedDocs.back().slice()};

      auto result = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testViewEdge SEARCH d._to == 'testCollection0/0' RETURN d");
      ASSERT_TRUE(result.result.ok());
      auto slice = result.data->slice();
      EXPECT_TRUE(slice.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(slice);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.begin();
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualDoc = resultIt.value();
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE((0 == darbotdb::basics::VelocyPackHelper::compare(
                              darbotdb::velocypack::Slice(*expectedDoc),
                              resolved, true)));
        ++expectedDoc;
      }
      EXPECT_FALSE(resultIt.valid());
      EXPECT_EQ(expectedDoc, expectedDocs.end());
    }
    // shortest path traversal
    {
      std::vector<darbotdb::velocypack::Slice> expectedDocs{
          _insertedDocs[6].slice(),
          _insertedDocs[7].slice(),
          _insertedDocs[5].slice(),
          _insertedDocs[0].slice(),
      };

      auto result = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR v, e IN OUTBOUND SHORTEST_PATH 'testCollection0/6' TO "
          "'testCollection0/5' edges FOR d IN testView SEARCH d.seq == v.seq "
          "SORT TFIDF(d) DESC, d.seq DESC, d._id RETURN d");
      ASSERT_TRUE(result.result.ok());
      auto slice = result.data->slice();
      EXPECT_TRUE(slice.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(slice);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.begin();
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualDoc = resultIt.value();
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE((0 == darbotdb::basics::VelocyPackHelper::compare(
                              darbotdb::velocypack::Slice(*expectedDoc),
                              resolved, true)));
        ++expectedDoc;
      }
      EXPECT_FALSE(resultIt.valid());
      EXPECT_EQ(expectedDoc, expectedDocs.end());
    }

    // simple traversal
    {
      std::vector<darbotdb::velocypack::Slice> expectedDocs{
          _insertedDocs[5].slice(), _insertedDocs[4].slice(),
          _insertedDocs[3].slice(), _insertedDocs[2].slice(),
          _insertedDocs[1].slice(),
      };

      auto result = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR v, e, p IN 1..2 OUTBOUND 'testCollection0/0' edges FOR d IN "
          "testView SEARCH d.seq == v.seq SORT TFIDF(d) DESC, d.seq DESC "
          "RETURN "
          "v");
      ASSERT_TRUE(result.result.ok());
      auto slice = result.data->slice();
      EXPECT_TRUE(slice.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(slice);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.begin();
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualDoc = resultIt.value();
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE((0 == darbotdb::basics::VelocyPackHelper::compare(
                              darbotdb::velocypack::Slice(*expectedDoc),
                              resolved, true)));
        ++expectedDoc;
      }
      EXPECT_FALSE(resultIt.valid());
      EXPECT_EQ(expectedDoc, expectedDocs.end());
    }
  }
};

class QueryTraversalView : public QueryTraversal {
 protected:
  ViewType type() const final { return darbotdb::ViewType::kArangoSearch; }
};

class QueryTraversalSearch : public QueryTraversal {
 protected:
  ViewType type() const final { return darbotdb::ViewType::kSearchAlias; }
};

TEST_P(QueryTraversalView, Test) {
  create();
  createView(R"("trackListPositions": true,)", R"()");
  // create view on edge collection
  {
    auto createJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testViewEdge\", \"type\": \"arangosearch\" }");
    auto logicalView = _vocbase.createView(createJson->slice(), false);
    ASSERT_FALSE(!logicalView);

    auto* view = logicalView.get();
    auto* impl = dynamic_cast<darbotdb::iresearch::IResearchView*>(view);
    ASSERT_FALSE(!impl);

    auto updateJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"links\": {"
        "\"edges\": { \"includeAllFields\": true }"
        "}}");
    EXPECT_TRUE(impl->properties(updateJson->slice(), true, true).ok());
    std::set<darbotdb::DataSourceId> cids;
    impl->visitCollections(
        [&cids](darbotdb::DataSourceId cid, darbotdb::LogicalView::Indexes*) {
          cids.emplace(cid);
          return true;
        });
    EXPECT_EQ(1U, cids.size());
    EXPECT_TRUE(
        (darbotdb::tests::executeQuery(_vocbase,
                                       "FOR d IN testViewEdge SEARCH 1 ==1 "
                                       "OPTIONS { waitForSync: true } RETURN d")
             .result.ok()));  // commit
  }
  queryTests();
}

TEST_P(QueryTraversalSearch, Test) {
  create();
  createIndexes(R"("trackListPositions": true,)", R"()");
  createSearch();
  // create index for edge collection
  {
    bool created = false;
    auto createJson = VPackParser::fromJson(absl::Substitute(
        R"({ "name": "edgesIndex", "type": "inverted",
               "version": $0,
               "includeAllFields": true })",
        version()));
    auto collection = _vocbase.lookupCollection("edges");
    ASSERT_TRUE(collection);
    collection->createIndex(createJson->slice(), created).waitAndGet();
    ASSERT_TRUE(created);
  }
  // create view on edge collection
  {
    auto createJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testViewEdge\", \"type\": \"search-alias\" }");
    auto logicalView = _vocbase.createView(createJson->slice(), false);
    ASSERT_FALSE(!logicalView);

    auto* view = logicalView.get();
    auto* impl = dynamic_cast<darbotdb::iresearch::Search*>(view);
    ASSERT_FALSE(!impl);
    auto viewDefinition = R"({"indexes": [
        {"collection": "edges", "index": "edgesIndex"}
      ]})";
    auto updateJson = darbotdb::velocypack::Parser::fromJson(viewDefinition);
    auto r = impl->properties(updateJson->slice(), true, true);
    EXPECT_TRUE(r.ok()) << r.errorMessage();
    std::set<darbotdb::DataSourceId> cids;
    impl->visitCollections(
        [&cids](darbotdb::DataSourceId cid, darbotdb::LogicalView::Indexes*) {
          cids.emplace(cid);
          return true;
        });
    EXPECT_EQ(1U, cids.size());
    EXPECT_TRUE(
        (darbotdb::tests::executeQuery(_vocbase,
                                       "FOR d IN testViewEdge SEARCH 1 == 1 "
                                       "OPTIONS { waitForSync: true } RETURN d")
             .result.ok()));  // commit
  }
  queryTests();
}

INSTANTIATE_TEST_CASE_P(IResearch, QueryTraversalView, GetLinkVersions());

INSTANTIATE_TEST_CASE_P(IResearch, QueryTraversalSearch, GetIndexVersions());

}  // namespace
}  // namespace darbotdb::tests
