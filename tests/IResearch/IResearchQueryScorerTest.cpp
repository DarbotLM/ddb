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

#include <regex>

#include "Aql/ExecutionNode/CalculationNode.h"
#include "Aql/ExecutionNode/IResearchViewNode.h"
#include "Aql/ExecutionPlan.h"
#include "Aql/Expression.h"
#include "Aql/OptimizerRule.h"
#include "Aql/Query.h"
#include "Async/async.h"
#include "IResearch/IResearchFeature.h"
#include "IResearch/IResearchVPackComparer.h"
#include "IResearch/IResearchView.h"
#include "IResearch/IResearchViewSort.h"
#include "IResearchQueryCommon.h"
#include "RestServer/QueryRegistryFeature.h"
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

class QueryScorer : public QueryTest {
 protected:
  std::deque<std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
      _insertedDocs;

  void create() {
    // add collection_1
    {
      auto collectionJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"collection_1\" }");
      auto logicalCollection1 =
          _vocbase.createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection1);
    }

    // add collection_2
    {
      auto collectionJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"collection_2\" }");
      auto logicalCollection2 =
          _vocbase.createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection2);
    }

    // add collection_3
    {
      auto collectionJson = darbotdb::velocypack::Parser::fromJson(
          "{ \"name\": \"collection_3\" }");
      auto logicalCollection3 =
          _vocbase.createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection3);
    }
  }

  void populateData() {
    auto logicalCollection1 = _vocbase.lookupCollection("collection_1");
    ASSERT_TRUE(logicalCollection1);
    auto logicalCollection2 = _vocbase.lookupCollection("collection_2");
    ASSERT_TRUE(logicalCollection2);
    auto logicalCollection3 = _vocbase.lookupCollection("collection_3");
    ASSERT_TRUE(logicalCollection2);
    // populate view with the data
    {
      darbotdb::OperationOptions opt;

      darbotdb::transaction::Methods trx(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          kEmpty,
          {logicalCollection1->name(), logicalCollection2->name(),
           logicalCollection3->name()},
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

      // insert into collection_3
      std::deque<std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>>
          insertedDocsCollection;

      {
        std::filesystem::path resource;
        resource /= std::string_view(darbotdb::tests::testResourceDir);
        resource /= std::string_view("simple_sequential_order.json");

        auto builder = darbotdb::basics::VelocyPackHelper::velocyPackFromFile(
            resource.string());
        auto root = builder.slice();
        ASSERT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection3->name(), doc, opt);
          EXPECT_TRUE(res.ok());

          res = trx.document(logicalCollection3->name(), res.slice(), opt);
          EXPECT_TRUE(res.ok());
          insertedDocsCollection.emplace_back(std::move(res.buffer));
        }
      }

      EXPECT_TRUE(trx.commit().ok());
      EXPECT_TRUE((darbotdb::tests::executeQuery(
                       _vocbase,
                       "FOR d IN testView SEARCH 1 ==1 OPTIONS "
                       "{ waitForSync: true } RETURN d")
                       .result.ok()));  // commit
    }
  }

  void queryTests() {
    // wrong number of arguments
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A') "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(
          TRI_ERROR_QUERY_FUNCTION_ARGUMENT_NUMBER_MISMATCH));
    }

    // invalid argument
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', {}) "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }

    // invalid argument
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', []) "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }

    // invalid argument
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', true) "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }

    // invalid argument
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', null) "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }

    // invalid argument
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', '42') "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }

    // non-deterministic argument
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', RAND()) "
          "RETURN { d, score: BOOSTSCORER(d) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_FALSE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }
    // constexpr BOOST (true)
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(1==1, 42) "
          "LIMIT 1 "
          "RETURN { d, score: BOOSTSCORER(d) }";
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.data->slice().isArray());
      ASSERT_EQ(1U, queryResult.data->slice().length());
    }
    // constexpr BOOST (false)
    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(1==2, 42) "
          "LIMIT 1 "
          "RETURN { d, score: BOOSTSCORER(d) }";
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());
      ASSERT_TRUE(queryResult.data->slice().isArray());
      ASSERT_EQ(0U, queryResult.data->slice().length());
    }

    {
      std::string const query =
          "FOR d IN testView SEARCH BOOST(d.name == 'A', 42) "
          "RETURN { d, score: BOOSTSCORER(d) }";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      std::map<float, darbotdb::velocypack::Slice> expectedDocs{
          {42.f, darbotdb::velocypack::Slice(_insertedDocs[0]->data())}};

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      // Check documents
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();

        auto actualScoreSlice = actualValue.get("score");
        ASSERT_TRUE(actualScoreSlice.isNumber());
        auto const actualScore = actualScoreSlice.getNumber<float>();
        auto expectedValue = expectedDocs.find(actualScore);
        ASSERT_NE(expectedValue, expectedDocs.end());

        auto const actualDoc = actualValue.get("d");
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE(
            (0 == darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(expectedValue->second),
                      resolved, true)));
        expectedDocs.erase(expectedValue);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    {
      std::string const query =
          "LET arr = [0,1] "
          "FOR i in 0..1 "
          "  LET rnd = _NONDETERM_(i) "
          "  FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "LIMIT 10 "
          "RETURN { d, score: d.seq + 3*customscorer(d, arr[TO_NUMBER(rnd != "
          "0)]) }";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      std::map<size_t, darbotdb::velocypack::Slice> expectedDocs{
          {0, darbotdb::velocypack::Slice(_insertedDocs[0]->data())},
          {1, darbotdb::velocypack::Slice(_insertedDocs[1]->data())},
          {2, darbotdb::velocypack::Slice(_insertedDocs[2]->data())},
          {3, darbotdb::velocypack::Slice(_insertedDocs[0]->data())},
          {4, darbotdb::velocypack::Slice(_insertedDocs[1]->data())},
          {5, darbotdb::velocypack::Slice(_insertedDocs[2]->data())},
      };

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      // Check documents
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();

        auto actualScoreSlice = actualValue.get("score");
        ASSERT_TRUE(actualScoreSlice.isNumber());
        auto const actualScore = actualScoreSlice.getNumber<size_t>();
        auto expectedValue = expectedDocs.find(actualScore);
        ASSERT_NE(expectedValue, expectedDocs.end());

        auto const actualDoc = actualValue.get("d");
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE(
            (0 == darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(expectedValue->second),
                      resolved, true)));
        expectedDocs.erase(expectedValue);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // ensure subqueries outstide a loop work fine
    {
      std::string const query =
          "LET x = (FOR j IN testView SEARCH j.name == 'A' SORT BM25(j) RETURN "
          "j) "
          "FOR d in testView SEARCH d.name == 'B' "
          "SORT customscorer(d, x[0].seq) "
          "RETURN { d, 'score' : customscorer(d, x[0].seq) }";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, query,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      std::map<size_t, darbotdb::velocypack::Slice> expectedDocs{
          {0, darbotdb::velocypack::Slice(_insertedDocs[1]->data())},
      };

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(expectedDocs.size(), resultIt.size());

      // Check documents
      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();

        auto actualScoreSlice = actualValue.get("score");
        ASSERT_TRUE(actualScoreSlice.isNumber());
        auto const actualScore = actualScoreSlice.getNumber<size_t>();
        auto expectedValue = expectedDocs.find(actualScore);
        ASSERT_NE(expectedValue, expectedDocs.end());

        auto const actualDoc = actualValue.get("d");
        auto const resolved = actualDoc.resolveExternals();

        EXPECT_TRUE(
            (0 == darbotdb::basics::VelocyPackHelper::compare(
                      darbotdb::velocypack::Slice(expectedValue->second),
                      resolved, true)));
        expectedDocs.erase(expectedValue);
      }
      EXPECT_TRUE(expectedDocs.empty());
    }

    // FIXME
    // inline subqueries aren't supported, e.g. the query below will be
    // transformed into
    //
    // FOR d in testView SEARCH d.name == 'B' LET #1 = customscorer(d,
    // #2[0].seq) LET #2 = (FOR j IN testView SEARCH j.name == 'A' SORT BM25(j)
    // RETURN j) RETURN { d, 'score' : #1 ) }
    {
      std::map<size_t, std::string_view> expectedDocs{{0, "B"}};

      std::string const query =
          "FOR d in testView SEARCH d.name == 'B' "
          "RETURN { d, 'score' : customscorer(d, (FOR j IN testView SEARCH "
          "j.name == 'A' SORT BM25(j) RETURN j)[0].seq) }";

      auto queryResult = darbotdb::tests::explainQuery(
          _vocbase, query, nullptr,
          "{ \"optimizer\": { \"rules\": [\"-move-calculations-up\", "
          "\"-move-calculations-up-2\"]}}");
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
      ASSERT_TRUE(std::regex_search(
          std::string(queryResult.errorMessage()),
          std::regex("variable '.' is used in search function.*CUSTOMSCORER")));

      // need to turn off certain optimizations so that the independent subquery
      // is not moved out of the FOR loop
      queryResult = darbotdb::tests::executeQuery(
          _vocbase, query, nullptr,
          "{ \"optimizer\": { \"rules\": [\"-move-calculations-up\", "
          "\"-move-calculations-up-2\"]}}");
      ASSERT_TRUE(queryResult.result.is(TRI_ERROR_BAD_PARAMETER));
    }

    // test that moves an unrelated subquery out of the loop (same case as
    // above, but with the subquery moved)
    {
      std::map<size_t, std::string_view> expectedDocs{{0, "B"}};

      std::string const query =
          "FOR d in testView SEARCH d.name == 'B' "
          "RETURN { d, 'score' : customscorer(d, (FOR j IN testView SEARCH "
          "j.name == 'A' SORT BM25(j) RETURN j)[0].seq) }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(1U, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isObject());

        auto actualScoreSlice = actualValue.get("score");
        ASSERT_TRUE(actualScoreSlice.isNumber());
        auto const actualScore = actualScoreSlice.getNumber<size_t>();
        auto expectedValue = expectedDocs.find(actualScore);
        ASSERT_NE(expectedValue, expectedDocs.end());

        auto const actualName = actualValue.get("d").get("name");
        ASSERT_EQ(expectedValue->second, actualName.copyString());
      }
    }

    // test case covers:
    // https://github.com/darbotdb/darbotdb/issues/9660
    {
      std::map<size_t, std::string_view> expectedDocs{{2, "A"}};

      std::string const query =
          "LET x = FIRST(FOR y IN collection_1 FILTER y.seq == 0 RETURN "
          "DISTINCT "
          "y.name) "
          "FOR d IN testView SEARCH d.name == x "
          "LET score = customscorer(d, 1) + 1.0 "
          "COLLECT name = d.name AGGREGATE maxScore = MAX(score) "
          "RETURN { name: name, score: maxScore }";

      auto queryResult = darbotdb::tests::executeQuery(_vocbase, query);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(1U, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isObject());

        auto actualScoreSlice = actualValue.get("score");
        ASSERT_TRUE(actualScoreSlice.isNumber());
        auto const actualScore = actualScoreSlice.getNumber<size_t>();
        auto expectedValue = expectedDocs.find(actualScore);
        ASSERT_NE(expectedValue, expectedDocs.end());

        auto const actualName = actualValue.get("name");
        ASSERT_EQ(expectedValue->second, actualName.copyString());
      }
    }

    // ensure scorers are deduplicated
    {
      std::string const queryString =
          "LET i = 1"
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'B', true, false) "
          "RETURN [ customscorer(d, i), customscorer(d, 1) ] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_VALUE, arg1->type);
        ASSERT_EQ(darbotdb::aql::VALUE_TYPE_INT, arg1->value.type);
        ASSERT_EQ(1, arg1->getIntValue());
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(1U, nodes.size());
      auto* calcNode =
          darbotdb::aql::ExecutionNode::castTo<darbotdb::aql::CalculationNode*>(
              nodes.front());
      ASSERT_TRUE(calcNode);
      ASSERT_TRUE(calcNode->expression());
      auto* node = calcNode->expression()->node();
      ASSERT_TRUE(node);
      ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, node->type);
      ASSERT_EQ(2, node->numMembers());
      for (size_t i = 0; i < node->numMembers(); ++i) {
        auto* sub = node->getMember(i);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
        EXPECT_EQ(static_cast<const void*>(var), sub->getData());
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(1U, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());

        for (; scoreIt.valid(); scoreIt.next()) {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(1, value.getNumber<size_t>());
        }
      }
    }

    // ensure scorers are deduplicated (attribute access)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_({ value : 2 }) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj.value), customscorer(d, obj.value) ] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());

        for (; scoreIt.valid(); scoreIt.next()) {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(2, value.getNumber<size_t>());
        }
      }
    }

    // ensure scorers are deduplicated (expression)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_({ value : 2 }) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj.value+1), customscorer(d, obj.value+1) "
          "] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_PLUS, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());

        for (; scoreIt.valid(); scoreIt.next()) {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(3, value.getNumber<size_t>());
        }
      }
    }

    // ensure scorers are deduplicated (indexed access)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj[1]), customscorer(d, obj[1]) ] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_INDEXED_ACCESS, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());

        for (; scoreIt.valid(); scoreIt.next()) {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(5, value.getNumber<size_t>());
        }
      }
    }

    // ensure scorers are deduplicated (ternary)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj[0] > obj[1] ? 1 : 2), customscorer(d, "
          "obj[0] > obj[1] ? 1 : 2) ] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_TERNARY, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());

        for (; scoreIt.valid(); scoreIt.next()) {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(2, value.getNumber<size_t>());
        }
      }
    }

    // ensure scorers aren't deduplicated (ternary)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj[0] > obj[1] ? 1 : 2), customscorer(d, "
          "obj[1] > obj[2] ? 1 : 2) ] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(2, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorers
      {
        auto* expr = scorers[0].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_TERNARY, arg1->type);
      }

      {
        auto* expr = scorers[1].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_TERNARY, arg1->type);
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());
        ASSERT_TRUE(scoreIt.valid());

        {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(2, value.getNumber<size_t>());
          scoreIt.next();
        }

        {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(1, value.getNumber<size_t>());
          scoreIt.next();
        }

        ASSERT_FALSE(scoreIt.valid());
      }
    }

    // ensure scorers are deduplicated (complex expression)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, 5*obj[0]*TO_NUMBER(obj[1] > obj[2])/obj[1] "
          "- "
          "1), customscorer(d, 5*obj[0]*TO_NUMBER(obj[1] > obj[2])/obj[1] - 1) "
          "] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_MINUS, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());

        for (; scoreIt.valid(); scoreIt.next()) {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(1, value.getNumber<size_t>());
        }
      }
    }

    // ensure scorers are deduplicated (dynamic object attribute name)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, { [ CONCAT(obj[0], obj[1]) ] : 1 }), "
          "customscorer(d, { [ CONCAT(obj[0], obj[1]) ] : 1 }) ]";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OBJECT, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }
    }

    // ensure scorers are deduplicated (dynamic object value)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, { foo : obj[1] }), customscorer(d, { foo : "
          "obj[1] }) ]";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OBJECT, arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }
    }

    // ensure scorers aren't deduplicated (complex expression)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, 5*obj[0]*TO_NUMBER(obj[1] > obj[2])/obj[1] "
          "- "
          "1), customscorer(d, 5*obj[0]*TO_NUMBER(obj[1] > obj[2])/obj[1] - 2) "
          "] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(2, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorers
      {
        auto* expr = scorers[0].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_MINUS, arg1->type);
      }

      {
        auto* expr = scorers[1].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_MINUS, arg1->type);
      }

      // check execution
      auto queryResult = darbotdb::tests::executeQuery(_vocbase, queryString);
      ASSERT_TRUE(queryResult.result.ok());

      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);
      ASSERT_EQ(3, resultIt.size());

      for (; resultIt.valid(); resultIt.next()) {
        auto const actualValue = resultIt.value();
        ASSERT_TRUE(actualValue.isArray());

        VPackArrayIterator scoreIt(actualValue);
        EXPECT_EQ(2, scoreIt.size());
        ASSERT_TRUE(scoreIt.valid());

        {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(1, value.getNumber<size_t>());
          scoreIt.next();
        }

        {
          auto const value = scoreIt.value();
          ASSERT_TRUE(value.isNumber());
          EXPECT_EQ(0, value.getNumber<size_t>());
          scoreIt.next();
        }

        ASSERT_FALSE(scoreIt.valid());
      }
    }

    // ensure scorers are deduplicated (array comparison operators)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj any == 3), customscorer(d, obj any == "
          "3) "
          "]";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(1U, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorer
      {
        auto* expr = scorers.front().node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_EQ,
                  arg1->type);
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(2, nodes.size());
      for (auto const* node : nodes) {
        auto* calcNode = darbotdb::aql::ExecutionNode::castTo<
            darbotdb::aql::CalculationNode const*>(node);
        ASSERT_TRUE(calcNode);
        ASSERT_TRUE(calcNode->expression());

        if (calcNode->outVariable()->name == "obj") {
          continue;
        }

        auto* exprNode = calcNode->expression()->node();
        ASSERT_TRUE(exprNode);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, exprNode->type);
        ASSERT_EQ(2, exprNode->numMembers());
        for (size_t i = 0; i < exprNode->numMembers(); ++i) {
          auto* sub = exprNode->getMember(i);
          ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
          EXPECT_EQ(static_cast<const void*>(var), sub->getData());
        }
      }
    }

    // ensure scorers aren't deduplicated (array comparison operator)
    {
      std::string const queryString =
          "LET obj = _NONDETERM_([ 2, 5 ]) "
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ customscorer(d, obj any == 3), customscorer(d, obj all == "
          "3) "
          "]";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // only one scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto& scorers = viewNode->scorers();
      ASSERT_EQ(2, scorers.size());
      auto* var = scorers.front().var;
      ASSERT_TRUE(var);

      // check scorers
      {
        auto* expr = scorers[0].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_EQ,
                  arg1->type);
      }

      {
        auto* expr = scorers[1].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("CUSTOMSCORER", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        EXPECT_EQ(darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_EQ,
                  arg1->type);
      }
    }

    // con't deduplicate scorers with default values
    {
      std::string const queryString =
          "FOR d IN testView SEARCH IN_RANGE(d.name, 'A', 'C', true, true) "
          "RETURN [ tfidf(d), tfidf(d, false) ] ";

      EXPECT_TRUE(darbotdb::tests::assertRules(
          _vocbase, queryString,
          {
              darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule,
          }));

      auto query = darbotdb::aql::Query::create(
          darbotdb::transaction::StandaloneContext::create(
              _vocbase, darbotdb::transaction::OperationOriginTestCase{}),
          darbotdb::aql::QueryString(queryString), nullptr);

      waitForAsync(query->prepareQuery());
      auto* plan = query->plan();
      ASSERT_TRUE(plan);

      darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;

      // 2 scorers scorer
      plan->findNodesOfType(
          nodes, darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW, true);
      ASSERT_EQ(1U, nodes.size());
      auto* viewNode = darbotdb::aql::ExecutionNode::castTo<
          darbotdb::iresearch::IResearchViewNode*>(nodes.front());
      ASSERT_TRUE(viewNode);
      auto scorers = viewNode->scorers();
      std::sort(scorers.begin(), scorers.end(),
                [](auto const& lhs, auto const& rhs) noexcept {
                  return lhs.var->name < rhs.var->name;
                });

      // check "tfidf(d)" scorer
      {
        auto* expr = scorers[0].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("TFIDF", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(1, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
      }

      // check "tfidf(d, false)" scorer
      {
        auto* expr = scorers[1].node;
        ASSERT_TRUE(expr);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_FCALL, expr->type);
        auto* fn = static_cast<darbotdb::aql::Function*>(expr->getData());
        ASSERT_TRUE(fn);
        ASSERT_TRUE(darbotdb::iresearch::isScorer(*fn));
        EXPECT_EQ("TFIDF", fn->name);

        ASSERT_EQ(1, expr->numMembers());
        auto* args = expr->getMember(0);
        ASSERT_TRUE(args);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, args->type);
        ASSERT_EQ(2, args->numMembers());
        auto* arg0 = args->getMember(0);  // reference to d
        ASSERT_TRUE(arg0);
        ASSERT_EQ(static_cast<void const*>(&viewNode->outVariable()),
                  arg0->getData());
        auto* arg1 = args->getMember(1);
        ASSERT_TRUE(arg1);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_VALUE, arg1->type);
        ASSERT_EQ(darbotdb::aql::VALUE_TYPE_BOOL, arg1->value.type);
        ASSERT_FALSE(arg1->getBoolValue());
      }

      // and 2 references
      nodes.clear();
      plan->findNodesOfType(nodes, darbotdb::aql::ExecutionNode::CALCULATION,
                            true);
      ASSERT_EQ(1U, nodes.size());
      auto* calcNode =
          darbotdb::aql::ExecutionNode::castTo<darbotdb::aql::CalculationNode*>(
              nodes.front());
      ASSERT_TRUE(calcNode);
      ASSERT_TRUE(calcNode->expression());
      auto* node = calcNode->expression()->node();
      ASSERT_TRUE(node);
      ASSERT_EQ(darbotdb::aql::NODE_TYPE_ARRAY, node->type);
      ASSERT_EQ(2, node->numMembers());

      for (size_t i = 0; i < node->numMembers(); ++i) {
        auto* sub = node->getMember(i);
        ASSERT_EQ(darbotdb::aql::NODE_TYPE_REFERENCE, sub->type);
        EXPECT_EQ(static_cast<const void*>(scorers[i].var), sub->getData());
      }
    }
  }
};

class QueryScorerView : public QueryScorer {
 protected:
  ViewType type() const final { return darbotdb::ViewType::kArangoSearch; }

  void createView() {
    auto createJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"arangosearch\" }");

    // add view
    auto view = std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
        _vocbase.createView(createJson->slice(), false));
    ASSERT_FALSE(!view);

    // add link to collection
    {
      auto viewDefinitionTemplate = R"({
        "links": {
          "collection_1": {
            "analyzers": [ "test_analyzer", "identity"],
            "includeAllFields": true,
            "version": $0,
            "trackListPositions": true },
          "collection_2": {
            "analyzers": [ "test_analyzer", "identity"],
            "version": $1,
            "includeAllFields": true }
      }})";

      auto viewDefinition = absl::Substitute(
          viewDefinitionTemplate, static_cast<uint32_t>(linkVersion()),
          static_cast<uint32_t>(linkVersion()));

      auto updateJson = VPackParser::fromJson(viewDefinition);

      EXPECT_TRUE(view->properties(updateJson->slice(), true, true).ok());

      darbotdb::velocypack::Builder builder;

      builder.openObject();
      auto res = view->properties(
          builder, darbotdb::LogicalDataSource::Serialization::Properties);
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

class QueryScorerSearch : public QueryScorer {
 protected:
  ViewType type() const final { return darbotdb::ViewType::kSearchAlias; }

  void createSearch() {
    // create indexes
    auto createIndex = [this](int name) {
      bool created = false;
      auto createJson = VPackParser::fromJson(absl::Substitute(
          R"({ "name": "index_$0", "type": "inverted",
               "version": $1,
               "trackListPositions": $2,
               "includeAllFields": true })",
          name, version(), (name == 1 ? "true" : "false")));
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

TEST_P(QueryScorerView, Test) {
  create();
  createView();
  populateData();
  queryTests();
}

TEST_P(QueryScorerSearch, Test) {
  create();
  createSearch();
  populateData();
  queryTests();
}

INSTANTIATE_TEST_CASE_P(IResearch, QueryScorerView, GetLinkVersions());

INSTANTIATE_TEST_CASE_P(IResearch, QueryScorerSearch, GetIndexVersions());

}  // namespace
}  // namespace darbotdb::tests
