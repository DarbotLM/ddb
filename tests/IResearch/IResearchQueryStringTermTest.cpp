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

#include "Aql/OptimizerRule.h"
#include "IResearch/IResearchVPackComparer.h"
#include "IResearch/IResearchView.h"
#include "IResearch/IResearchViewSort.h"
#include "IResearchQueryCommon.h"
#include "Transaction/Helpers.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/OperationOptions.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"
#include "store/mmap_directory.hpp"
#include "utils/index_utils.hpp"

namespace darbotdb::tests {
namespace {

static std::vector<std::string> const kEmpty;

class QueryStringTerm : public QueryTest {
 protected:
  std::deque<std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
      _insertedDocs;

  void create() {
    std::shared_ptr<darbotdb::LogicalCollection> logicalCollection1;
    std::shared_ptr<darbotdb::LogicalCollection> logicalCollection2;

    // add collection_1
    {
      auto collectionJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"collection_1\" }");
      logicalCollection1 = _vocbase.createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection1);
    }

    // add collection_2
    {
      auto collectionJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"collection_2\" }");
      logicalCollection2 = _vocbase.createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection2);
    }
  }

  void populateData() {
    auto logicalCollection1 = _vocbase.lookupCollection("collection_1");
    ASSERT_TRUE(logicalCollection1);
    auto logicalCollection2 = _vocbase.lookupCollection("collection_2");
    ASSERT_TRUE(logicalCollection2);

    darbotdb::OperationOptions opt;

    darbotdb::transaction::Methods trx(
        darbotdb::transaction::StandaloneContext::create(
            _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
        kEmpty, {logicalCollection1->name(), logicalCollection2->name()},
        kEmpty, darbotdb::transaction::Options());
    EXPECT_TRUE(trx.begin().ok());

    // insert into collections
    {
      std::filesystem::path resource;
      resource /= std::string_view(darbotdb::tests::testResourceDir);
      resource /= std::string_view("simple_sequential.json");

      auto builder = darbotdb::basics::VelocyPackHelper::velocyPackFromFile(
          resource.string());
      auto root = builder.slice();
      ASSERT_TRUE(root.isArray());

      size_t i = 0;

      std::shared_ptr<darbotdb::LogicalCollection> collections[]{
          logicalCollection1, logicalCollection2};

      for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
        auto res = trx.insert(collections[i % 2]->name(), doc, opt);
        EXPECT_TRUE(res.ok());

        res = trx.document(collections[i % 2]->name(), res.slice(), opt);
        EXPECT_TRUE(res.ok());
        _insertedDocs.emplace_back(std::move(res.buffer));
        ++i;
      }
    }

    EXPECT_TRUE(trx.commit().ok());
    EXPECT_TRUE((darbotdb::tests::executeQuery(
                     _vocbase,
                     "FOR d IN testView SEARCH 1 ==1 OPTIONS { "
                     "waitForSync: true } RETURN d")
                     .result.ok()));  // commit
  }

  void queryTests() {
    // darbotdb specific string comparer
    struct StringComparer {
      bool operator()(std::string_view lhs, std::string_view rhs) const {
        return darbotdb::basics::VelocyPackHelper::compareStringValues(
                   lhs.data(), lhs.size(), rhs.data(), rhs.size(), true) < 0;
      }
    };

    // ==, !=, <, <=, >, >=, range

    // -----------------------------------------------------------------------------
    // --SECTION--                                                 system
    // attributes
    // -----------------------------------------------------------------------------

    // _rev attribute
    {
      auto rev = darbotdb::transaction::helpers::extractRevSliceFromDocument(
          VPackSlice(_insertedDocs.front()->data()));
      auto const revRef = darbotdb::iresearch::getStringRef(rev);

      std::string const query = "FOR d IN testView SEARCH d._rev == '" +
                                static_cast<std::string>(revRef) + "' RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}};

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // _key attribute
    {
      auto key = darbotdb::transaction::helpers::extractKeyPart(
          VPackSlice(_insertedDocs.front()->data())
              .get(darbotdb::StaticStrings::KeyString));

      std::string const query = "FOR d IN testView SEARCH d._key == '" +
                                std::string(key) + "' RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}};

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // _id attribute
    {
      darbotdb::transaction::Methods trx(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          kEmpty, kEmpty, kEmpty, darbotdb::transaction::Options());

      auto const id =
          trx.extractIdString(VPackSlice(_insertedDocs.front()->data()));
      std::string const query =
          "FOR d IN testView SEARCH d._id == '" + id + "' RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}};

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // -----------------------------------------------------------------------------
    // --SECTION-- ==
    // -----------------------------------------------------------------------------

    // missing term
    {
      std::string const query =
          "FOR d IN testView SEARCH d.name == 'invalid_value' RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      std::string const query = "FOR d IN testView SEARCH d.name == 0 RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      std::string const query =
          "FOR d IN testView SEARCH d.name == null RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      std::string const query =
          "FOR d IN testView SEARCH d.name == false RETURN d";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name == true RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name == @name RETURN d",
          darbotdb::velocypack::Parser::fromJson("{ \"name\" : true }"));
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name == 'A', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name == 'A' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.same == 'same', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.same == 'xyz' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.same == 'same', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.same == CONCAT('xy', @param) RETURN d",
          darbotdb::velocypack::Parser::fromJson("{ \"param\" : \"z\" }"));
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.duplicated == 'abcd', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]},  {"E", _insertedDocs[4]},
                       {"K", _insertedDocs[10]}, {"U", _insertedDocs[20]},
                       {"~", _insertedDocs[26]}, {"$", _insertedDocs[30]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.duplicated == 'abcd' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.duplicated == 'abcd', name DESC
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>,
               StringComparer>
          expectedDocs{{"A", _insertedDocs[0]},  {"E", _insertedDocs[4]},
                       {"K", _insertedDocs[10]}, {"U", _insertedDocs[20]},
                       {"~", _insertedDocs[26]}, {"$", _insertedDocs[30]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.duplicated == "
          "'abcd' SORT d.name DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator actualDocs(result);
      EXPECT_EQ(expectedDocs.size(), actualDocs.size());

      for (auto expectedDoc = expectedDocs.rbegin(), end = expectedDocs.rend();
           expectedDoc != end; ++expectedDoc) {
        EXPECT_TRUE(actualDocs.valid());
        auto actualDoc = actualDocs.value();
        auto const resolved = actualDoc.resolveExternals();
        std::string const actualName = resolved.get("name").toString();
        std::string const expectedName =
            darbotdb::velocypack::Slice(expectedDoc->second->data())
                .get("name")
                .toString();

        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        actualDocs.next();
      }
      EXPECT_FALSE(actualDocs.valid());
    }

    // d.duplicated == 'abcd', TFIDF() ASC, name DESC
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>,
               StringComparer>
          expectedDocs{{"A", _insertedDocs[0]},  {"E", _insertedDocs[4]},
                       {"K", _insertedDocs[10]}, {"U", _insertedDocs[20]},
                       {"~", _insertedDocs[26]}, {"$", _insertedDocs[30]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.duplicated == 'abcd' SORT TFIDF(d) ASC, "
          "d.name DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator actualDocs(result);
      EXPECT_EQ(expectedDocs.size(), actualDocs.size());

      for (auto expectedDoc = expectedDocs.rbegin(), end = expectedDocs.rend();
           expectedDoc != end; ++expectedDoc) {
        EXPECT_TRUE(actualDocs.valid());
        auto actualDoc = actualDocs.value();
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        actualDocs.next();
      }
      EXPECT_FALSE(actualDocs.valid());
    }

    // d.same == 'same', BM25() ASC, TFIDF() ASC, seq DESC
    {
      auto const& expectedDocs = _insertedDocs;

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.same == 'xyz' SORT BM25(d) ASC, TFIDF(d) "
          "DESC, d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(0 ==
                    darbotdb::basics::VelocyPackHelper::compare(
                        darbotdb::velocypack::Slice((*expectedDoc)->data()),
                        resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // expression  (invalid value)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x = RAND()"
          "LET z = {} "
          "FOR d IN testView SEARCH z.name == (x + (RAND() + 1)) RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // FIXME(SEARCH-83) support expression with self-reference
    // expression  (invalid value)
    //{
    //  auto queryResult = darbotdb::tests::executeQuery(
    //      _vocbase,
    //      "LET x = RAND()"
    //      "FOR d IN testView SEARCH d.name == (x + (RAND() + 1)) RETURN d");
    //  ASSERT_TRUE(queryResult.result.ok()) <<
    //  queryResult.result.errorMessage();
    //
    //  auto result = queryResult.data->slice();
    //  EXPECT_TRUE(result.isArray());
    //
    //  darbotdb::velocypack::ArrayIterator resultIt(result);
    //  EXPECT_EQ(0U, resultIt.size());
    //}

    // expression, d.duplicated == 'abcd', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]},  {"E", _insertedDocs[4]},
                       {"K", _insertedDocs[10]}, {"U", _insertedDocs[20]},
                       {"~", _insertedDocs[26]}, {"$", _insertedDocs[30]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x = _NONDETERM_('abcd') "
          "FOR d IN testView SEARCH d.duplicated == x RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // expression+variable, d.duplicated == 'abcd', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]},  {"E", _insertedDocs[4]},
                       {"K", _insertedDocs[10]}, {"U", _insertedDocs[20]},
                       {"~", _insertedDocs[26]}, {"$", _insertedDocs[30]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x = _NONDETERM_('abc') "
          "FOR d IN testView SEARCH d.duplicated == CONCAT(x, 'd') RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // expression+variable, d.duplicated == 'abcd', unordered, LIMIT 2
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}, {"E", _insertedDocs[4]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x = _NONDETERM_('abc') "
          "FOR d IN testView SEARCH d.duplicated == "
          "CONCAT(x, 'd') LIMIT 2 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // expression, d.duplicated == 'abcd', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]},  {"E", _insertedDocs[4]},
                       {"K", _insertedDocs[10]}, {"U", _insertedDocs[20]},
                       {"~", _insertedDocs[26]}, {"$", _insertedDocs[30]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.duplicated == "
          "CONCAT(_FORWARD_('abc'), 'd') RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // subquery, d.name == (FOR i IN collection_1 SEARCH i.name == 'A' RETURN
    // i)[0].name), unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x=(FOR i IN collection_1 FILTER "
          "i.name=='A' RETURN i)[0].name FOR d "
          "IN testView SEARCH d.name==x RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // subquery, d.name == (FOR i IN collection_1 SEARCH i.name == 'A' RETURN
    // i)[0]), unordered
    {
      auto queryResult =
          darbotdb::tests::executeQuery(_vocbase,
                                        "LET x=(FOR i IN collection_1 FILTER "
                                        "i.name=='A' RETURN i)[0] FOR d IN "
                                        "testView SEARCH d.name==x RETURN d");
      ASSERT_TRUE(queryResult.result.is(
          TRI_ERROR_BAD_PARAMETER));  // unsupported type: object
    }

    // subquery, d.name == (FOR i IN collection_1 SEARCH i.name == 'A' RETURN
    // i)[0].name), unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs{{"A", _insertedDocs[0]}};

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name==(FOR i IN collection_1 FILTER "
          "i.name=='A' RETURN i)[0].name RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // subquery, d.name == (FOR i IN collection_1 SEARCH i.name == 'A' RETURN
    // i)[0]), unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name==(FOR i IN collection_1 FILTER "
          "i.name=='A' RETURN i)[0] RETURN d");
      ASSERT_TRUE(queryResult.result.is(
          TRI_ERROR_BAD_PARAMETER));  // unsupported type: object
    }

    // -----------------------------------------------------------------------------
    // --SECTION-- !=
    // -----------------------------------------------------------------------------

    // invalid type, unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name != 0 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // invalid type, unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name != false RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // invalid type, d.seq DESC
    {
      std::map<ptrdiff_t,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("seq");
        expectedDocs.emplace(keySlice.getNumber<ptrdiff_t>(), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name != null SORT d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // missing term, unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name != 'invalid_term' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // existing duplicated term, unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.same != 'xyz' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // existing unique term, unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      expectedDocs.erase("C");

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name != 'C' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // existing unique term, unordered (not all documents contain field)
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;

      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const fieldSlice = docSlice.get("duplicated");

        if (!fieldSlice.isNone() &&
            "vczc" == darbotdb::iresearch::getStringRef(fieldSlice)) {
          continue;
        }

        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.duplicated != 'vczc' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // missing term, seq DESC
    {
      auto& expectedDocs = _insertedDocs;

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name != "
          "'invalid_term' SORT d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(0 ==
                    darbotdb::basics::VelocyPackHelper::compare(
                        darbotdb::velocypack::Slice((*expectedDoc)->data()),
                        resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // existing duplicated term, TFIDF() ASC, BM25() ASC, seq DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const fieldSlice = docSlice.get("duplicated");

        if (!fieldSlice.isNone() &&
            "abcd" == darbotdb::iresearch::getStringRef(fieldSlice)) {
          continue;
        }

        auto const keySlice = docSlice.get("seq");
        expectedDocs.emplace(keySlice.getNumber<size_t>(), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.duplicated != 'abcd' SORT TFIDF(d) ASC, "
          "BM25(d) ASC, d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(resultIt.size(), expectedDocs.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // expression: invalid type, unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        expectedDocs.emplace(darbotdb::iresearch::getStringRef(keySlice), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x = _NONDETERM_(0) "
          "FOR d IN testView SEARCH d.name != x RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // expression: existing duplicated term, TFIDF() ASC, BM25() ASC, seq DESC
    // LIMIT 10
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      size_t limit = 5;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const fieldSlice = docSlice.get("duplicated");

        if (!fieldSlice.isNone() &&
            "abcd" == darbotdb::iresearch::getStringRef(fieldSlice)) {
          continue;
        }

        auto const keySlice = docSlice.get("seq");
        expectedDocs.emplace(keySlice.getNumber<size_t>(), doc);
      }

      // limit results
      while (expectedDocs.size() > limit) {
        expectedDocs.erase(expectedDocs.begin());
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "LET x = _NONDETERM_('abc') "
          "FOR d IN testView SEARCH d.duplicated != CONCAT(x,'d') SORT "
          "TFIDF(d) "
          "ASC, BM25(d) ASC, d.seq DESC LIMIT 5 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(resultIt.size(), expectedDocs.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;

        if (!limit--) {
          break;
        }
      }

      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION-- <
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name < null RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name < true RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name < 0 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name < 'H', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key >= "H") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name < 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name < '!' (less than min term), unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name < '!' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name < '~' (less than max term), BM25() ASC, TFIDF() ASC seq DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const nameSlice = docSlice.get("name");
        if (darbotdb::iresearch::getStringRef(nameSlice) >= "~") {
          continue;
        }
        auto const keySlice = docSlice.get("seq");
        expectedDocs.emplace(keySlice.getNumber<size_t>(), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name < '~' SORT BM25(d), TFIDF(d), d.seq "
          "DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION-- <=
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name <= null RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name <= true RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name <= 0 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name <= 'H', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key > "H") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name <= 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name <= '!' (less than min term), unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name <= '!' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(1U, resultIt.size());

      auto const actualDoc = resultIt.value();
      auto const resolved = actualDoc.resolveExternals();
      EXPECT_TRUE(0 ==
                  darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(_insertedDocs[27]->data()),
                      resolved, true));

      resultIt.next();
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name <= '~' (less than max term), BM25() ASC, TFIDF() ASC seq DESC
    {
      auto& expectedDocs = _insertedDocs;

      auto queryResult =
          darbotdb::tests::executeQuery(_vocbase,
                                        "FOR d IN testView SEARCH d.name <= "
                                        "'~' SORT BM25(d), TFIDF(d), d.seq "
                                        "DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(0 ==
                    darbotdb::basics::VelocyPackHelper::compare(
                        darbotdb::velocypack::Slice((*expectedDoc)->data()),
                        resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION-- >
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name > null RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name > true RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name > 0 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name > 'H', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key <= "H") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name > 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name > '~' (greater than max term), unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name > '~' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name > '!' (greater than min term), BM25() ASC, TFIDF() ASC seq DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const nameSlice = docSlice.get("name");
        if (darbotdb::iresearch::getStringRef(nameSlice) <= "!") {
          continue;
        }
        auto const keySlice = docSlice.get("seq");
        expectedDocs.emplace(keySlice.getNumber<size_t>(), doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > '!' SORT BM25(d), TFIDF(d), d.seq "
          "DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION-- >=
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name >= null RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name >= true RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name >= 0 RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name >= 'H', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key < "H") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name >= 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name >= '~' (greater or equal than max term), unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name >= '~' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(1U, resultIt.size());

      auto const actualDoc = resultIt.value();
      auto const resolved = actualDoc.resolveExternals();
      EXPECT_TRUE(0 ==
                  darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(_insertedDocs[26]->data()),
                      resolved, true));

      resultIt.next();
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= '!' (greater or equal than min term), BM25() ASC, TFIDF() ASC
    // seq DESC
    {
      auto& expectedDocs = _insertedDocs;

      auto queryResult =
          darbotdb::tests::executeQuery(_vocbase,
                                        "FOR d IN testView SEARCH d.name >= "
                                        "'!' SORT BM25(d), TFIDF(d), d.seq "
                                        "DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(0 ==
                    darbotdb::basics::VelocyPackHelper::compare(
                        darbotdb::velocypack::Slice((*expectedDoc)->data()),
                        resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION--                                                      Range
    // (>,
    // <)
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > null AND d.name < 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > true AND d.name < 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 0 AND d.name < 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name > 'H' AND d.name < 'S', unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key <= "H" || key >= "S") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 'H' AND d.name < 'S' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name > 'S' AND d.name < 'N' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 'S' AND d.name < 'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name > 'H' AND d.name < 'H' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 'H' AND d.name < 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name > '!' AND d.name < '~' , TFIDF() ASC, BM25() ASC, d.sec DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("seq");
        auto const key = keySlice.getNumber<size_t>();
        auto const nameSlice = docSlice.get("name");
        auto const name = darbotdb::iresearch::getStringRef(nameSlice);
        if (name <= "!" || name >= "~") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult =
          darbotdb::tests::executeQuery(_vocbase,
                                        "FOR d IN testView SEARCH d.name > '!' "
                                        "AND d.name < '~' SORT tfidf(d), "
                                        "BM25(d), d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION--                                                     Range
    // (>=,
    // <)
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= null AND d.name < 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= true AND d.name < 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 0 AND d.name < 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name >= 'H' AND d.name < 'S' , unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key < "H" || key >= "S") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 'H' AND d.name < 'S' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name >= 'S' AND d.name < 'N' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 'S' AND d.name < 'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= 'H' AND d.name < 'H' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 'H' AND d.name < 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= '!' AND d.name < '~' , TFIDF() ASC, BM25() ASC, d.sec DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("seq");
        auto const key = keySlice.getNumber<size_t>();
        auto const nameSlice = docSlice.get("name");
        auto const name = darbotdb::iresearch::getStringRef(nameSlice);
        if (name < "!" || name >= "~") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= '!' "
          "AND d.name < '~' SORT tfidf(d), "
          "BM25(d), d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION--                                                     Range (>,
    // <=)
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > null AND d.name <= 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > true AND d.name <= 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 0 AND d.name <= 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name >= 'H' AND d.name <= 'S' , unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key <= "H" || key > "S") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 'H' AND d.name <= 'S' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name > 'S' AND d.name <= 'N' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 'S' AND d.name <= 'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name > 'H' AND d.name <= 'H' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name > 'H' AND d.name <= 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name > '!' AND d.name <= '~' , TFIDF() ASC, BM25() ASC, d.sec DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("seq");
        auto const key = keySlice.getNumber<size_t>();
        auto const nameSlice = docSlice.get("name");
        auto const name = darbotdb::iresearch::getStringRef(nameSlice);
        if (name <= "!" || name > "~") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult =
          darbotdb::tests::executeQuery(_vocbase,
                                        "FOR d IN testView SEARCH d.name > '!' "
                                        "AND d.name <= '~' SORT tfidf(d), "
                                        "BM25(d), d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION--                                                    Range (>=,
    // <=)
    // -----------------------------------------------------------------------------

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= null AND d.name <= 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= true AND d.name <= 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // invalid type
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 0 AND d.name <= 'Z' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());

      for (auto const actualDoc : resultIt) {
        IRS_IGNORE(actualDoc);
        EXPECT_TRUE(false);
      }
    }

    // d.name >= 'H' AND d.name <= 'S' , unordered
    {
      std::map<std::string_view,
               std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);
        if (key < "H" || key > "S") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 'H' AND d.name <= 'S' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        auto const keySlice = resolved.get("name");
        auto const key = darbotdb::iresearch::getStringRef(keySlice);

        auto expectedDoc = expectedDocs.find(key);
        ASSERT_NE(expectedDoc, expectedDocs.end());
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        expectedDocs.erase(expectedDoc);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // d.name >= 'S' AND d.name <= 'N' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 'S' AND d.name <= 'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= 'H' AND d.name <= 'H' , unordered
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= 'H' AND d.name <= 'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(1U, resultIt.size());
      EXPECT_TRUE(resultIt.valid());

      auto const resolved = resultIt.value().resolveExternals();
      EXPECT_TRUE(0 ==
                  darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(_insertedDocs[7]->data()),
                      resolved, true));

      resultIt.next();
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name > '!' AND d.name <= '~' , TFIDF() ASC, BM25() ASC, d.sec DESC
    {
      std::map<size_t, std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          expectedDocs;
      for (auto const& doc : _insertedDocs) {
        darbotdb::velocypack::Slice docSlice(doc->data());
        auto const keySlice = docSlice.get("seq");
        auto const key = keySlice.getNumber<size_t>();
        auto const nameSlice = docSlice.get("name");
        auto const name = darbotdb::iresearch::getStringRef(nameSlice);
        if (name < "!" || name > "~") {
          continue;
        }
        expectedDocs.emplace(key, doc);
      }

      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name >= '!' "
          "AND d.name <= '~' SORT tfidf(d), "
          "BM25(d), d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(expectedDocs.size(), resultIt.size());

      auto expectedDoc = expectedDocs.rbegin();
      for (auto const actualDoc : resultIt) {
        auto const resolved = actualDoc.resolveExternals();
        EXPECT_TRUE(
            0 == darbotdb::basics::VelocyPackHelper::compare(
                     darbotdb::velocypack::Slice(expectedDoc->second->data()),
                     resolved, true));
        ++expectedDoc;
      }
      EXPECT_EQ(expectedDoc, expectedDocs.rend());
    }

    // -----------------------------------------------------------------------------
    // --SECTION--                                                    Range (>=,
    // <=)
    // -----------------------------------------------------------------------------

    // d.name >= 'H' AND d.name <= 'S' , unordered
    // (will be converted to d.name >= 0 AND d.name <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name IN 'H'..'S' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.seq >= 'H' AND d.seq <= 'S' , unordered
    // (will be converted to d.seq >= 0 AND d.seq <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.seq IN 'H'..'S' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(1U, resultIt.size());
      EXPECT_TRUE(resultIt.valid());

      auto const resolved = resultIt.value().resolveExternals();
      EXPECT_TRUE(0 ==
                  darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(_insertedDocs[0]->data()),
                      resolved, true));

      resultIt.next();
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= 'S' AND d.name <= 'N' , unordered
    // (will be converted to d.name >= 0 AND d.name <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name IN 'S'..'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
    }

    // d.seq >= 'S' AND d.seq <= 'N' , unordered
    // (will be converted to d.seq >= 0 AND d.seq <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.seq IN 'S'..'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(1U, resultIt.size());
      EXPECT_TRUE(resultIt.valid());

      auto const resolved = resultIt.value().resolveExternals();
      EXPECT_TRUE(0 ==
                  darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(_insertedDocs[0]->data()),
                      resolved, true));

      resultIt.next();
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= 'H' AND d.name <= 'H' , unordered
    // (will be converted to d.name >= 0 AND d.name <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.name IN 'H'..'H' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
      ASSERT_TRUE(queryResult.result.ok());
    }

    // d.seq >= 'H' AND d.seq <= 'N' , unordered
    // (will be converted to d.seq >= 0 AND d.seq <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase, "FOR d IN testView SEARCH d.seq IN 'H'..'N' RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(1U, resultIt.size());
      EXPECT_TRUE(resultIt.valid());

      auto const resolved = resultIt.value().resolveExternals();
      EXPECT_TRUE(0 ==
                  darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(_insertedDocs[0]->data()),
                      resolved, true));

      resultIt.next();
      EXPECT_FALSE(resultIt.valid());
    }

    // d.name >= '!' AND d.name <= '~' , TFIDF() ASC, BM25() ASC, d.sec DESC
    // (will be converted to d.name >= 0 AND d.name <= 0)
    {
      auto queryResult = darbotdb::tests::executeQuery(
          _vocbase,
          "FOR d IN testView SEARCH d.name IN '!'..'~' SORT tfidf(d), BM25(d), "
          "d.seq DESC RETURN d");
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      EXPECT_EQ(0U, resultIt.size());
      EXPECT_FALSE(resultIt.valid());
      ASSERT_TRUE(queryResult.result.ok());
    }
  }
};

class QueryStringTermView : public QueryStringTerm {
 protected:
  ViewType type() const final { return darbotdb::ViewType::kArangoSearch; }

  void createView() {
    // add view
    auto createJson = darbotdb::velocypack::Parser::fromJson(
        "{ \
    \"name\": \"testView\", \
    \"type\": \"arangosearch\" \
  }");

    auto view = std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
        _vocbase.createView(createJson->slice(), false));
    ASSERT_FALSE(!view);

    // add link to collection
    {
      auto viewDefinitionTemplate = R"({
      "links": {
        "collection_1": {
          "includeAllFields": true,
          "version": $0 },
        "collection_2": {
          "version": $1,
          "includeAllFields": true }
    }})";

      auto viewDefinition = absl::Substitute(
          viewDefinitionTemplate, static_cast<uint32_t>(linkVersion()),
          static_cast<uint32_t>(linkVersion()));

      auto updateJson = darbotdb::velocypack::Parser::fromJson(viewDefinition);

      EXPECT_TRUE(view->properties(updateJson->slice(), true, true).ok());

      darbotdb::velocypack::Builder builder;

      builder.openObject();
      auto res = view->properties(builder,
                                  LogicalDataSource::Serialization::Properties);
      ASSERT_TRUE(res.ok());
      builder.close();

      auto slice = builder.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_EQ(slice.get("name").copyString(), "testView");
      EXPECT_TRUE(slice.get("type").copyString() ==
                  darbotdb::iresearch::StaticStrings::ViewArangoSearchType);
      EXPECT_TRUE(slice.get("deleted").isNone());  // no system properties
      auto tmpSlice = slice.get("links");
      EXPECT_TRUE(tmpSlice.isObject() && 2 == tmpSlice.length());
    }
  }
};

class QueryStringTermSearch : public QueryStringTerm {
 protected:
  ViewType type() const final { return darbotdb::ViewType::kSearchAlias; }

  void createSearch() {
    // create indexes
    auto createIndex = [this](int name) {
      bool created = false;
      auto createJson = VPackParser::fromJson(absl::Substitute(
          R"({ "name": "index_$0", "type": "inverted",
               "version": $1,
               "includeAllFields": true })",
          name, version()));
      auto collection =
          _vocbase.lookupCollection(absl::Substitute("collection_$0", name));
      ASSERT_TRUE(collection);
      collection->createIndex(createJson->slice(), created).waitAndGet();
      ASSERT_TRUE(created);
    };
    createIndex(1);
    createIndex(2);

    // add view
    auto createJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"search-alias\" }");

    auto view = std::dynamic_pointer_cast<darbotdb::iresearch::Search>(
        _vocbase.createView(createJson->slice(), false));
    ASSERT_FALSE(!view);

    // add link to collection
    {
      auto const viewDefinition = R"({
      "indexes": [
        { "collection": "collection_1", "index": "index_1"},
        { "collection": "collection_2", "index": "index_2"}
      ]})";
      auto updateJson = darbotdb::velocypack::Parser::fromJson(viewDefinition);
      auto r = view->properties(updateJson->slice(), true, true);
      EXPECT_TRUE(r.ok()) << r.errorMessage();
    }
  }
};

TEST_P(QueryStringTermView, Test) {
  create();
  createView();
  populateData();
  queryTests();
}

TEST_P(QueryStringTermSearch, Test) {
  create();
  createSearch();
  populateData();
  queryTests();
}

INSTANTIATE_TEST_CASE_P(IResearch, QueryStringTermView, GetLinkVersions());

INSTANTIATE_TEST_CASE_P(IResearch, QueryStringTermSearch, GetIndexVersions());

}  // namespace
}  // namespace darbotdb::tests
