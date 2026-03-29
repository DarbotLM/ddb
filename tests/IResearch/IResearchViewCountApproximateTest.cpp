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
/// @author Andrei Lobov
////////////////////////////////////////////////////////////////////////////////

#include "IResearchQueryCommon.h"
#include "Aql/AqlCall.h"
#include "Aql/ExecutionBlockImpl.h"
#include "Aql/ExecutionNode/IResearchViewNode.h"
#include "Aql/ExecutionPlan.h"
#include "Aql/Executor/IResearchViewExecutor.h"
#include "Aql/Executor/IResearchViewMergeExecutor.h"
#include "Aql/Query.h"
#include "Aql/SingleRowFetcher.h"
#include "Async/async.h"
#include "Basics/GlobalResourceMonitor.h"
#include "Basics/ResourceUsage.h"
#include "IResearch/IResearchExecutionPool.h"
#include "IResearch/IResearchLink.h"
#include "IResearch/IResearchLinkHelper.h"
#include "IResearch/IResearchView.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"

#include "IResearch/MakeViewSnapshot.h"

#include <velocypack/Iterator.h>
#include "frozen/map.h"

namespace {

static char const* collectionName1 = "collection_1";
static char const* collectionName2 = "collection_2";
static char const* viewName = "view";

static std::vector<darbotdb::iresearch::SearchFunc> emptyScorers;
static std::vector<darbotdb::iresearch::HeapSortElement> emptyScorersSort;

static constexpr frozen::map<std::string_view,
                             darbotdb::iresearch::CountApproximate, 2>
    countApproximationTypeMap = {
        {"exact", darbotdb::iresearch::CountApproximate::Exact},
        {"cost", darbotdb::iresearch::CountApproximate::Cost}};
}  // namespace

class IResearchViewCountApproximateTest : public IResearchQueryTest {
 protected:
  std::shared_ptr<darbotdb::iresearch::IResearchView> _view;
  darbotdb::iresearch::IResearchExecutionPool _pool{0, "", "", ""};
  IResearchViewCountApproximateTest() {
    // add collection_1
    std::shared_ptr<darbotdb::LogicalCollection> logicalCollection1;
    {
      auto collectionJson = VPackParser::fromJson(std::string("{\"name\": \"") +
                                                  collectionName1 + "\"}");
      logicalCollection1 = vocbase().createCollection(collectionJson->slice());
      EXPECT_NE(nullptr, logicalCollection1);
    }

    // add collection_2
    std::shared_ptr<darbotdb::LogicalCollection> logicalCollection2;
    {
      auto collectionJson = VPackParser::fromJson(std::string("{\"name\": \"") +
                                                  collectionName2 + "\"}");
      logicalCollection2 = vocbase().createCollection(collectionJson->slice());
      EXPECT_NE(nullptr, logicalCollection2);
    }
    // create view
    {
      auto createJson =
          VPackParser::fromJson(std::string("{") + "\"name\": \"" + viewName +
                                "\", \
           \"commitIntervalMsec\":0, \
           \"consolidationIntervalMsec\":0, \
           \"type\": \"arangosearch\", \
           \"primarySort\": [{\"field\": \"value\", \"direction\": \"asc\"}], \
           \"storedValues\": [] \
        }");
      _view = std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
          vocbase().createView(createJson->slice(), false));
      EXPECT_TRUE(_view);

      // add links to collections
      addLinkToCollection(_view);
    }

    // populate view with the data
    {
      darbotdb::OperationOptions opt;
      static std::vector<std::string> const EMPTY;
      darbotdb::transaction::Methods trx(
          darbotdb::transaction::StandaloneContext::create(
              vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
          EMPTY, {logicalCollection1->name(), logicalCollection2->name()},
          EMPTY, darbotdb::transaction::Options());
      EXPECT_TRUE(trx.begin().ok());

      // insert into collection_1
      {
        auto builder = VPackParser::fromJson(
            "["
            "{\"_key\": \"c0\", \"value\": 0},"
            "{\"_key\": \"c1\", \"value\": 1},"
            "{\"_key\": \"c2\", \"value\": 2},"
            "{\"_key\": \"c3\", \"value\": 3}"
            "]");

        auto root = builder->slice();
        EXPECT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection1->name(), doc, opt);
          EXPECT_TRUE(res.ok());
        }
      }

      // insert into collection_2
      {
        auto builder = VPackParser::fromJson(
            "["
            "{\"_key\": \"c_0\", \"value\": 10},"
            "{\"_key\": \"c_1\", \"value\": 11},"
            "{\"_key\": \"c_2\", \"value\": 12},"
            "{\"_key\": \"c_3\", \"value\": 13}"
            "]");

        auto root = builder->slice();
        EXPECT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection2->name(), doc, opt);
          EXPECT_TRUE(res.ok());
        }
      }

      EXPECT_TRUE(trx.commit().ok());

      EXPECT_TRUE(darbotdb::iresearch::IResearchLinkHelper::find(
                      *logicalCollection1, *_view)
                      ->commit()
                      .ok());

      EXPECT_TRUE(darbotdb::iresearch::IResearchLinkHelper::find(
                      *logicalCollection2, *_view)
                      ->commit()
                      .ok());
    }
    // now we need to have at least 2 segments per index to check proper inter
    // segment switches. So - another round of commits to force creating new
    // segment. And as we have no consolidations - segments will be not merged
    {
      darbotdb::OperationOptions opt;
      static std::vector<std::string> const EMPTY;
      darbotdb::transaction::Methods trx(
          darbotdb::transaction::StandaloneContext::create(
              vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
          EMPTY, {logicalCollection1->name(), logicalCollection2->name()},
          EMPTY, darbotdb::transaction::Options());
      EXPECT_TRUE(trx.begin().ok());

      // insert into collection_1
      {
        auto builder = VPackParser::fromJson(
            "["
            "{\"_key\": \"c4\", \"value\": 4},"
            "{\"_key\": \"c5\", \"value\": 5},"
            "{\"_key\": \"c6\", \"value\": 6},"
            "{\"_key\": \"c7\", \"value\": 7},"
            "{\"_key\": \"c8\", \"value\": 10}"
            "]");

        auto root = builder->slice();
        EXPECT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection1->name(), doc, opt);
          EXPECT_TRUE(res.ok());
        }
      }

      // insert into collection_2
      {
        auto builder = VPackParser::fromJson(
            "["
            "{\"_key\": \"c_4\", \"value\": 14},"
            "{\"_key\": \"c_5\", \"value\": 15},"
            "{\"_key\": \"c_6\", \"value\": 16},"
            "{\"_key\": \"c_7\", \"value\": 17}"
            "]");

        auto root = builder->slice();
        EXPECT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection2->name(), doc, opt);
          EXPECT_TRUE(res.ok());
        }
      }

      EXPECT_TRUE(trx.commit().ok());

      EXPECT_TRUE(darbotdb::iresearch::IResearchLinkHelper::find(
                      *logicalCollection1, *_view)
                      ->commit()
                      .ok());

      EXPECT_TRUE(darbotdb::iresearch::IResearchLinkHelper::find(
                      *logicalCollection2, *_view)
                      ->commit()
                      .ok());
    }
  }

  void addLinkToCollection(
      std::shared_ptr<darbotdb::iresearch::IResearchView>& view) {
    auto updateJson = VPackParser::fromJson(
        std::string("{") +
        "\"links\": {"
        "\"" +
        collectionName1 +
        "\": {\"includeAllFields\": true, \"storeValues\": \"id\"},"
        "\"" +
        collectionName2 +
        "\": {\"includeAllFields\": true, \"storeValues\": \"id\"}"
        "}}");
    EXPECT_TRUE(view->properties(updateJson->slice(), true, true).ok());

    darbotdb::velocypack::Builder builder;

    builder.openObject();
    auto res = view->properties(
        builder, darbotdb::LogicalDataSource::Serialization::Properties);
    ASSERT_TRUE(res.ok());
    builder.close();

    auto slice = builder.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(slice.get("type").copyString() ==
                darbotdb::iresearch::StaticStrings::ViewArangoSearchType);
    EXPECT_TRUE(slice.get("deleted").isNone());  // no system properties
    auto tmpSlice = slice.get("links");
    EXPECT_TRUE(tmpSlice.isObject() && 2 == tmpSlice.length());
  }

  void executeAndCheck(
      std::string const& queryString,
      std::vector<VPackValue> const* expectedValues, int64_t expectedFullCount,
      darbotdb::iresearch::CountApproximate expectedApproximation) {
    SCOPED_TRACE(testing::Message("Query:") << queryString);

    auto explain = darbotdb::tests::explainQuery(
        vocbase(), queryString, nullptr,
        expectedFullCount >= 0 ? "{\"fullCount\":true}" : "{}");
    ASSERT_TRUE(explain.data);
    auto const explanation = explain.data->slice();

    darbotdb::velocypack::ArrayIterator nodes(explanation.get("nodes"));

    bool viewFound{false};
    darbotdb::iresearch::CountApproximate actualApproximate{
        darbotdb::iresearch::CountApproximate::Exact};
    for (auto const& node : nodes) {
      if (node.get("type").stringView() == "EnumerateViewNode") {
        viewFound = true;
        auto optionsSlice = node.get("options");
        ASSERT_TRUE(optionsSlice.isObject());
        auto approximationSlice = optionsSlice.get("countApproximate");
        if (!approximationSlice.isNone()) {
          ASSERT_TRUE(approximationSlice.isString());
          auto it =
              countApproximationTypeMap.find(approximationSlice.stringView());
          ASSERT_NE(it, countApproximationTypeMap.end());
          actualApproximate = it->second;
        }
        break;
      }
    }
    ASSERT_TRUE(viewFound);
    ASSERT_EQ(expectedApproximation, actualApproximate);

    auto query = darbotdb::aql::Query::create(
        darbotdb::transaction::StandaloneContext::create(
            vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
        darbotdb::aql::QueryString(queryString), nullptr);

    auto queryResult = darbotdb::tests::executeQuery(
        vocbase(), queryString, nullptr,
        expectedFullCount >= 0 ? "{\"fullCount\":true}" : "{}");
    ASSERT_TRUE(queryResult.result.ok());
    if (expectedFullCount >= 0) {
      ASSERT_NE(nullptr, queryResult.extra);
      auto statsSlice = queryResult.extra->slice().get("stats");
      ASSERT_TRUE(statsSlice.isObject());
      auto fullCountSlice = statsSlice.get("fullCount");
      ASSERT_TRUE(fullCountSlice.isInteger());
      ASSERT_EQ(expectedFullCount, fullCountSlice.getInt());
    }

    if (expectedValues) {
      auto result = queryResult.data->slice();
      EXPECT_TRUE(result.isArray());

      darbotdb::velocypack::ArrayIterator resultIt(result);

      ASSERT_EQ(expectedValues->size(), resultIt.size());
      // Check values
      auto expectedValue = expectedValues->begin();
      for (; resultIt.valid(); resultIt.next(), ++expectedValue) {
        auto const actualDoc = resultIt.value();
        auto const resolved = actualDoc.resolveExternals();

        if (resolved.isString()) {
          ASSERT_TRUE(expectedValue->isString());
          darbotdb::velocypack::ValueLength length = 0;
          auto resStr = resolved.getString(length);
          EXPECT_EQ(memcmp(expectedValue->getCharPtr(), resStr, length), 0);
        } else {
          ASSERT_TRUE(resolved.isNumber());
          EXPECT_EQ(expectedValue->getInt64(), resolved.getInt());
        }
      }
      EXPECT_EQ(expectedValue, expectedValues->end());
    }
  }
};

TEST_F(IResearchViewCountApproximateTest, fullCountExact) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " COLLECT WITH COUNT INTO c RETURN c ";

  std::vector<VPackValue> expectedValues{
      VPackValue(17),
  };
  executeAndCheck(queryString, &expectedValues, -1,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, fullCountCost) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " OPTIONS {countApproximate:'cost'} COLLECT WITH COUNT INTO c RETURN c ";

  std::vector<VPackValue> expectedValues{
      VPackValue(17),
  };
  executeAndCheck(queryString, &expectedValues, -1,
                  darbotdb::iresearch::CountApproximate::Cost);
}

TEST_F(IResearchViewCountApproximateTest, fullCountWithFilter) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 10 COLLECT WITH COUNT INTO c RETURN c ";

  std::vector<VPackValue> expectedValues{
      VPackValue(9),
  };
  executeAndCheck(queryString, &expectedValues, -1,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, fullCountWithFilterEmpty) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 10000 COLLECT WITH COUNT INTO c RETURN c ";

  std::vector<VPackValue> expectedValues{
      VPackValue(0),
  };
  executeAndCheck(queryString, &expectedValues, -1,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, fullCountWithFilterCost) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 10 OPTIONS {countApproximate:'cost'} COLLECT WITH "
      "COUNT INTO c RETURN c ";

  std::vector<VPackValue> expectedValues{
      VPackValue(9),
  };
  executeAndCheck(queryString, &expectedValues, -1,
                  darbotdb::iresearch::CountApproximate::Cost);
}

TEST_F(IResearchViewCountApproximateTest, fullCountWithFilterCostEmpty) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 10000 OPTIONS {countApproximate:'cost'} COLLECT WITH "
      "COUNT INTO c RETURN c ";

  std::vector<VPackValue> expectedValues{
      VPackValue(0),
  };
  executeAndCheck(queryString, &expectedValues, -1,
                  darbotdb::iresearch::CountApproximate::Cost);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountWithFilter) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 10 OPTIONS {countApproximate:'exact'} LIMIT 2, 2 "
      "RETURN  d.value ";
  executeAndCheck(queryString, nullptr, 9,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountWithFilterSorted) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 2 OPTIONS {countApproximate:'exact'} SORT d.value "
      "ASC LIMIT 1  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(2),
  };
  executeAndCheck(queryString, &expectedValues, 15,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountSorted) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " OPTIONS {countApproximate:'exact'} SORT d.value "
                           "ASC LIMIT 7, 1  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(7),
  };
  executeAndCheck(queryString, &expectedValues, 17,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountSortedCost) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " OPTIONS {countApproximate:'cost'} SORT d.value "
                           "ASC LIMIT 7, 1  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(7),
  };
  executeAndCheck(queryString, &expectedValues, 17,
                  darbotdb::iresearch::CountApproximate::Cost);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountNotSorted) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " OPTIONS {countApproximate:'exact'} SORT d.value "
                           "DESC LIMIT 7, 1  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(10),
  };
  executeAndCheck(queryString, &expectedValues, 17,
                  darbotdb::iresearch::CountApproximate::Exact);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountNotSortedCost) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " OPTIONS {countApproximate:'cost'} SORT d.value "
                           "DESC LIMIT 7, 1  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(10),
  };
  executeAndCheck(queryString, &expectedValues, 17,
                  darbotdb::iresearch::CountApproximate::Cost);
}

TEST_F(IResearchViewCountApproximateTest, forcedFullCountWithFilterSortedCost) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 2 OPTIONS {countApproximate:'cost'} SORT d.value ASC "
      "LIMIT 8, 1  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(11),
  };
  executeAndCheck(queryString, &expectedValues, 15,
                  darbotdb::iresearch::CountApproximate::Cost);
}

TEST_F(IResearchViewCountApproximateTest,
       forcedFullCountWithFilterNoOffsetSortedCost) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 2 OPTIONS {countApproximate:'cost'} SORT d.value ASC "
      "LIMIT  2  RETURN  d.value ";

  std::vector<VPackValue> expectedValues{
      VPackValue(2),
      VPackValue(3),
  };
  executeAndCheck(queryString, &expectedValues, 15,
                  darbotdb::iresearch::CountApproximate::Cost);
}

// This corner-case is currently impossible as there are no way to get skipAll
// without prior call to skip for MergeExecutor. But in future this might happen
// and the skippAll method should still be correct (as it is correct call)
TEST_F(IResearchViewCountApproximateTest, directSkipAllForMergeExecutorExact) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 2 OPTIONS {countApproximate:'exact', "
      "\"noMaterialization\":false} SORT d.value ASC "
      " COLLECT WITH COUNT INTO c   RETURN c ";
  auto query = darbotdb::aql::Query::create(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      darbotdb::aql::QueryString(queryString), nullptr);
  darbotdb::tests::waitForAsync(query->prepareQuery());
  ASSERT_TRUE(query->ast());
  auto plan =
      darbotdb::aql::ExecutionPlan::instantiateFromAst(query->ast(), false);
  plan->planRegisters();

  darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;
  plan->findNodesOfType(
      nodes, {darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW}, true);
  ASSERT_EQ(1, nodes.size());
  auto& viewNode = *darbotdb::aql::ExecutionNode::castTo<
      darbotdb::iresearch::IResearchViewNode*>(nodes.front());
  static std::vector<std::string> const EMPTY;
  darbotdb::aql::RegIdSetStack regsToKeep{
      1};  // we need at least one register to keep
  darbotdb::aql::RegisterInfos registerInfos = darbotdb::aql::RegisterInfos{
      {},        {}, 0, 0, viewNode.getRegsToClear(),
      regsToKeep};  // completely dummy. But we will not execute pipeline
                    // anyway. Just make ctor happy.
  darbotdb::transaction::Methods trx(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      EMPTY, EMPTY, EMPTY, darbotdb::transaction::Options());
  ASSERT_TRUE(trx.state());
  auto snapshot =
      makeViewSnapshot(trx, darbotdb::iresearch::ViewSnapshotMode::FindOrCreate,
                       _view->getLinks(nullptr), _view.get(), _view->name());
  auto reader = darbotdb::iresearch::ViewSnapshotPtr{
      darbotdb::iresearch::ViewSnapshotPtr{}, snapshot};
  darbotdb::iresearch::IResearchViewSort sort;
  sort.emplace_back({{std::string_view("value"), false}}, true);
  darbotdb::aql::IResearchViewExecutorInfos executorInfos(
      reader, darbotdb::aql::RegisterId::makeInvalid(),
      darbotdb::aql::RegisterId::makeInvalid(), {}, *query,
#ifdef USE_ENTERPRISE
      _view->meta()._optimizeTopK,
#endif
      emptyScorers, {&sort, 1U}, _view->storedValues(), *plan,
      viewNode.outVariable(), viewNode.filterCondition(), {false, false}, 0,
      viewNode.getRegisterPlan()->varInfo, 0,
      darbotdb::iresearch::IResearchViewNode::ViewValuesRegisters{},
      darbotdb::iresearch::CountApproximate::Exact,
      darbotdb::iresearch::FilterOptimization::MAX, emptyScorersSort, 0,
      nullptr, 1, _pool);

  std::vector<darbotdb::aql::ExecutionBlock*> emptyExecutors;
  darbotdb::aql::DependencyProxy<darbotdb::aql::BlockPassthrough::Disable>
      dummyProxy(emptyExecutors, 0);
  darbotdb::aql::SingleRowFetcher<darbotdb::aql::BlockPassthrough::Disable>
      fetcher(dummyProxy);
  darbotdb::GlobalResourceMonitor global{};
  darbotdb::ResourceMonitor monitor{global};
  darbotdb::aql::AqlItemBlockManager itemBlockManager{monitor};
  size_t skippedLocal = 0;
  darbotdb::aql::AqlCall call{};
  darbotdb::aql::IResearchViewStats stats;
  darbotdb::aql::ExecutorState state = darbotdb::aql::ExecutorState::HASMORE;
  darbotdb::aql::IResearchViewMergeExecutor<darbotdb::aql::ExecutionTraits<
      false, false, false,
      darbotdb::iresearch::MaterializeType::NotMaterialize>>
      mergeExecutor(fetcher, executorInfos);
  darbotdb::aql::SharedAqlItemBlockPtr inputBlock =
      itemBlockManager.requestBlock(1, 1);
  inputBlock->setValue(0, 0, darbotdb::aql::AqlValue("dummy"));
  darbotdb::aql::AqlCall skipAllCall{0U, 0U, 0U, true};
  darbotdb::aql::AqlItemBlockInputRange inputRange(
      darbotdb::aql::MainQueryState::DONE, 0, inputBlock, 0);
  std::tie(state, stats, skippedLocal, call) =
      mergeExecutor.skipRowsRange(inputRange, skipAllCall);
  ASSERT_EQ(15, skipAllCall.getSkipCount());
}

TEST_F(IResearchViewCountApproximateTest,
       directSkipAllForMergeExecutorExactEmpty) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 1000000 OPTIONS {countApproximate:'exact', "
      "\"noMaterialization\":false} SORT d.value ASC "
      " COLLECT WITH COUNT INTO c   RETURN c ";
  auto query = darbotdb::aql::Query::create(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      darbotdb::aql::QueryString(queryString), nullptr);
  darbotdb::tests::waitForAsync(query->prepareQuery());
  ASSERT_TRUE(query->ast());
  auto plan =
      darbotdb::aql::ExecutionPlan::instantiateFromAst(query->ast(), false);
  plan->planRegisters();

  darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;
  plan->findNodesOfType(
      nodes, {darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW}, true);
  ASSERT_EQ(1, nodes.size());
  auto& viewNode = *darbotdb::aql::ExecutionNode::castTo<
      darbotdb::iresearch::IResearchViewNode*>(nodes.front());
  static std::vector<std::string> const EMPTY;
  darbotdb::aql::RegIdSetStack regsToKeep{
      1};  // we need at least one register to keep
  darbotdb::aql::RegisterInfos registerInfos = darbotdb::aql::RegisterInfos{
      {},        {}, 0, 0, viewNode.getRegsToClear(),
      regsToKeep};  // completely dummy. But we will not execute pipeline
                    // anyway. Just make ctor happy.
  darbotdb::transaction::Methods trx(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      EMPTY, EMPTY, EMPTY, darbotdb::transaction::Options());
  auto snapshot =
      makeViewSnapshot(trx, darbotdb::iresearch::ViewSnapshotMode::FindOrCreate,
                       _view->getLinks(nullptr), _view.get(), _view->name());
  auto reader = darbotdb::iresearch::ViewSnapshotPtr{
      darbotdb::iresearch::ViewSnapshotPtr{}, snapshot};
  darbotdb::iresearch::IResearchViewSort sort;
  sort.emplace_back({{std::string_view("value"), false}}, true);
  darbotdb::aql::IResearchViewExecutorInfos executorInfos(
      reader, darbotdb::aql::RegisterId::makeInvalid(),
      darbotdb::aql::RegisterId::makeInvalid(), {}, *query,
#ifdef USE_ENTERPRISE
      _view->meta()._optimizeTopK,
#endif
      emptyScorers, {&sort, 1U}, _view->storedValues(), *plan,
      viewNode.outVariable(), viewNode.filterCondition(), {false, false}, 0,
      viewNode.getRegisterPlan()->varInfo, 0,
      darbotdb::iresearch::IResearchViewNode::ViewValuesRegisters{},
      darbotdb::iresearch::CountApproximate::Exact,
      darbotdb::iresearch::FilterOptimization::MAX, emptyScorersSort, 0,
      nullptr, 1, _pool);

  std::vector<darbotdb::aql::ExecutionBlock*> emptyExecutors;
  darbotdb::aql::DependencyProxy<darbotdb::aql::BlockPassthrough::Disable>
      dummyProxy(emptyExecutors, 0);
  darbotdb::aql::SingleRowFetcher<darbotdb::aql::BlockPassthrough::Disable>
      fetcher(dummyProxy);
  darbotdb::GlobalResourceMonitor global{};
  darbotdb::ResourceMonitor monitor{global};
  darbotdb::aql::AqlItemBlockManager itemBlockManager{monitor};
  darbotdb::aql::SharedAqlItemBlockPtr inputBlock =
      itemBlockManager.requestBlock(1, 1);
  darbotdb::aql::IResearchViewMergeExecutor<darbotdb::aql::ExecutionTraits<
      false, false, false,
      darbotdb::iresearch::MaterializeType::NotMaterialize>>
      mergeExecutor(fetcher, executorInfos);
  size_t skippedLocal = 0;
  darbotdb::aql::AqlCall call{};
  darbotdb::aql::IResearchViewStats stats;
  darbotdb::aql::ExecutorState state = darbotdb::aql::ExecutorState::HASMORE;
  inputBlock->setValue(0, 0, darbotdb::aql::AqlValue("dummy"));
  darbotdb::aql::AqlCall skipAllCall{0U, 0U, 0U, true};
  darbotdb::aql::AqlItemBlockInputRange inputRange(
      darbotdb::aql::MainQueryState::DONE, 0, inputBlock, 0);
  std::tie(state, stats, skippedLocal, call) =
      mergeExecutor.skipRowsRange(inputRange, skipAllCall);
  ASSERT_EQ(0, skipAllCall.getSkipCount());
}

// This corner-case is currently impossible as there are no way to get skipAll
// without prior call to skip for MergeExecutor. But in future this might happen
// and the skippAll method should still be correct (as it is correct call)
TEST_F(IResearchViewCountApproximateTest, directSkipAllForMergeExecutorCost) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value >= 2 OPTIONS {countApproximate:'cost', "
      "\"noMaterialization\":false} SORT d.value ASC "
      " COLLECT WITH COUNT INTO c   RETURN c ";
  auto query = darbotdb::aql::Query::create(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      darbotdb::aql::QueryString(queryString), nullptr);
  darbotdb::tests::waitForAsync(query->prepareQuery());
  ASSERT_TRUE(query->ast());
  auto plan =
      darbotdb::aql::ExecutionPlan::instantiateFromAst(query->ast(), false);
  plan->planRegisters();

  darbotdb::containers::SmallVector<darbotdb::aql::ExecutionNode*, 8> nodes;
  plan->findNodesOfType(
      nodes, {darbotdb::aql::ExecutionNode::ENUMERATE_IRESEARCH_VIEW}, true);
  ASSERT_EQ(1, nodes.size());
  auto& viewNode = *darbotdb::aql::ExecutionNode::castTo<
      darbotdb::iresearch::IResearchViewNode*>(nodes.front());
  static std::vector<std::string> const EMPTY;
  darbotdb::aql::RegIdSetStack regsToKeep{
      1};  // we need at least one register to keep
  darbotdb::aql::RegisterInfos registerInfos = darbotdb::aql::RegisterInfos{
      {},        {}, 0, 0, viewNode.getRegsToClear(),
      regsToKeep};  // completely dummy. But we will not execute pipeline
                    // anyway. Just make ctor happy.
  darbotdb::transaction::Methods trx(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      EMPTY, EMPTY, EMPTY, darbotdb::transaction::Options());
  auto snapshot =
      makeViewSnapshot(trx, darbotdb::iresearch::ViewSnapshotMode::FindOrCreate,
                       _view->getLinks(nullptr), _view.get(), _view->name());
  auto reader = darbotdb::iresearch::ViewSnapshotPtr{
      darbotdb::iresearch::ViewSnapshotPtr{}, snapshot};
  darbotdb::iresearch::IResearchViewSort sort;
  sort.emplace_back({{std::string_view("value"), false}}, true);
  darbotdb::aql::IResearchViewExecutorInfos executorInfos(
      reader, darbotdb::aql::RegisterId::makeInvalid(),
      darbotdb::aql::RegisterId::makeInvalid(), {}, *query,
#ifdef USE_ENTERPRISE
      _view->meta()._optimizeTopK,
#endif
      emptyScorers, {&sort, 1U}, _view->storedValues(), *plan,
      viewNode.outVariable(), viewNode.filterCondition(), {false, false}, 0,
      viewNode.getRegisterPlan()->varInfo, 0,
      darbotdb::iresearch::IResearchViewNode::ViewValuesRegisters{},
      darbotdb::iresearch::CountApproximate::Cost,
      darbotdb::iresearch::FilterOptimization::MAX, emptyScorersSort, 0,
      nullptr, 1, _pool);

  std::vector<darbotdb::aql::ExecutionBlock*> emptyExecutors;
  darbotdb::aql::DependencyProxy<darbotdb::aql::BlockPassthrough::Disable>
      dummyProxy(emptyExecutors, 0);
  darbotdb::aql::SingleRowFetcher<darbotdb::aql::BlockPassthrough::Disable>
      fetcher(dummyProxy);
  darbotdb::GlobalResourceMonitor global{};
  darbotdb::ResourceMonitor monitor{global};
  darbotdb::aql::AqlItemBlockManager itemBlockManager{monitor};
  darbotdb::aql::SharedAqlItemBlockPtr inputBlock =
      itemBlockManager.requestBlock(1, 1);
  darbotdb::aql::IResearchViewMergeExecutor<darbotdb::aql::ExecutionTraits<
      false, false, false,
      darbotdb::iresearch::MaterializeType::NotMaterialize>>
      mergeExecutor(fetcher, executorInfos);
  size_t skippedLocal = 0;
  darbotdb::aql::AqlCall skipAllCall{0U, 0U, 0U, true};
  darbotdb::aql::AqlCall call{};
  darbotdb::aql::IResearchViewStats stats;
  darbotdb::aql::ExecutorState state = darbotdb::aql::ExecutorState::HASMORE;
  inputBlock->setValue(0, 0, darbotdb::aql::AqlValue("dummy"));
  darbotdb::aql::AqlItemBlockInputRange inputRange(
      darbotdb::aql::MainQueryState::DONE, 0, inputBlock, 0);
  std::tie(state, stats, skippedLocal, call) =
      mergeExecutor.skipRowsRange(inputRange, skipAllCall);
  ASSERT_EQ(15, skipAllCall.getSkipCount());
}
