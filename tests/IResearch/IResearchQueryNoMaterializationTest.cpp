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
/// @author Yuriy Popov
////////////////////////////////////////////////////////////////////////////////

#include <absl/strings/str_replace.h>

#include <velocypack/Iterator.h>

#include "Aql/AqlFunctionFeature.h"
#include "Aql/ExecutionNode/IResearchViewNode.h"
#include "Aql/OptimizerRule.h"
#include "Aql/Query.h"
#include "IResearch/ApplicationServerHelper.h"
#include "IResearch/IResearchLink.h"
#include "IResearch/IResearchLinkHelper.h"
#include "IResearch/IResearchView.h"
#include "IResearch/IResearchViewStoredValues.h"
#include "IResearchQueryCommon.h"
#include "RestServer/DatabaseFeature.h"
#include "RestServer/DatabasePathFeature.h"
#include "RestServer/FlushFeature.h"
#include "RestServer/SystemDatabaseFeature.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"

namespace darbotdb::tests {
namespace {

class QueryTestMulti
    : public ::testing::TestWithParam<
          std::tuple<darbotdb::ViewType, darbotdb::iresearch::LinkVersion>>,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 private:
  TRI_vocbase_t* _vocbase{nullptr};

 protected:
  darbotdb::tests::mocks::MockAqlServer server;

  virtual darbotdb::ViewType type() const { return std::get<0>(GetParam()); }

  QueryTestMulti() : server{false} {
    darbotdb::tests::init(true);

    server.addFeature<darbotdb::FlushFeature>(false);
    server.startFeatures();

    auto& analyzers =
        server.getFeature<darbotdb::iresearch::IResearchAnalyzerFeature>();
    darbotdb::iresearch::IResearchAnalyzerFeature::EmplaceResult result;

    auto& dbFeature = server.getFeature<darbotdb::DatabaseFeature>();
    // required for IResearchAnalyzerFeature::emplace(...)
    dbFeature.createDatabase(testDBInfo(server.server()), _vocbase);

    std::shared_ptr<darbotdb::LogicalCollection> unused;
    darbotdb::OperationOptions options(darbotdb::ExecContext::current());
    darbotdb::methods::Collections::createSystem(
        *_vocbase, options, darbotdb::tests::AnalyzerCollectionName, false,
        unused);
    unused = nullptr;

    auto res = analyzers.emplace(
        result, "testVocbase::test_analyzer", "TestAnalyzer",
        VPackParser::fromJson("\"abc\"")->slice(),
        darbotdb::transaction::OperationOriginTestCase{},
        darbotdb::iresearch::Features(
            {}, irs::IndexFeatures::FREQ |
                    irs::IndexFeatures::POS));  // required for PHRASE
    EXPECT_TRUE(res.ok());

    res = analyzers.emplace(
        result, "testVocbase::test_csv_analyzer", "TestDelimAnalyzer",
        VPackParser::fromJson("\",\"")->slice(),
        darbotdb::transaction::OperationOriginTestCase{});  // cache analyzer
    EXPECT_TRUE(res.ok());

    res = analyzers.emplace(
        result, "testVocbase::text_en", "text",
        VPackParser::fromJson(
            "{ \"locale\": \"en.UTF-8\", \"stopwords\": [ ] }")
            ->slice(),
        darbotdb::transaction::OperationOriginTestCase{},
        darbotdb::iresearch::Features{
            darbotdb::iresearch::FieldFeatures::NORM,
            irs::IndexFeatures::FREQ |
                irs::IndexFeatures::POS});  // cache analyzer
    EXPECT_TRUE(res.ok());

    auto sysVocbase =
        server.getFeature<darbotdb::SystemDatabaseFeature>().use();
    darbotdb::methods::Collections::createSystem(
        *sysVocbase, options, darbotdb::tests::AnalyzerCollectionName, false,
        unused);
    unused = nullptr;

    res =
        analyzers.emplace(result, "_system::test_analyzer", "TestAnalyzer",
                          VPackParser::fromJson("\"abc\"")->slice(),
                          darbotdb::transaction::OperationOriginTestCase{},
                          darbotdb::iresearch::Features{
                              irs::IndexFeatures::FREQ |
                              irs::IndexFeatures::POS});  // required for PHRASE

    res = analyzers.emplace(
        result, "_system::ngram_test_analyzer13", "ngram",
        VPackParser::fromJson("{\"min\":1, \"max\":3, \"streamType\":\"utf8\", "
                              "\"preserveOriginal\":false}")
            ->slice(),
        darbotdb::transaction::OperationOriginTestCase{},
        darbotdb::iresearch::Features{
            irs::IndexFeatures::FREQ |
            irs::IndexFeatures::POS});  // required for PHRASE

    res = analyzers.emplace(
        result, "_system::ngram_test_analyzer2", "ngram",
        VPackParser::fromJson("{\"min\":2, \"max\":2, \"streamType\":\"utf8\", "
                              "\"preserveOriginal\":false}")
            ->slice(),
        darbotdb::transaction::OperationOriginTestCase{},
        darbotdb::iresearch::Features{
            irs::IndexFeatures::FREQ |
            irs::IndexFeatures::POS});  // required for PHRASE

    EXPECT_TRUE(res.ok());

    res = analyzers.emplace(
        result, "_system::test_csv_analyzer", "TestDelimAnalyzer",
        VPackParser::fromJson("\",\"")->slice(),
        darbotdb::transaction::OperationOriginTestCase{});  // cache analyzer
    EXPECT_TRUE(res.ok());

    auto& functions = server.getFeature<darbotdb::aql::AqlFunctionFeature>();
    // register fake non-deterministic function in order to suppress
    // optimizations
    functions.add(darbotdb::aql::Function{
        "_NONDETERM_", ".",
        darbotdb::aql::Function::makeFlags(
            // fake non-deterministic
            darbotdb::aql::Function::Flags::CanRunOnDBServerCluster,
            darbotdb::aql::Function::Flags::CanRunOnDBServerOneShard),
        [](darbotdb::aql::ExpressionContext*, darbotdb::aql::AstNode const&,
           darbotdb::aql::functions::VPackFunctionParametersView params) {
          TRI_ASSERT(!params.empty());
          return params[0];
        }});

    // register fake non-deterministic function in order to suppress
    // optimizations
    functions.add(darbotdb::aql::Function{
        "_FORWARD_", ".",
        darbotdb::aql::Function::makeFlags(
            // fake deterministic
            darbotdb::aql::Function::Flags::Deterministic,
            darbotdb::aql::Function::Flags::Cacheable,
            darbotdb::aql::Function::Flags::CanRunOnDBServerCluster,
            darbotdb::aql::Function::Flags::CanRunOnDBServerOneShard),
        [](darbotdb::aql::ExpressionContext*, darbotdb::aql::AstNode const&,
           darbotdb::aql::functions::VPackFunctionParametersView params) {
          TRI_ASSERT(!params.empty());
          return params[0];
        }});

    // external function names must be registred in upper-case
    // user defined functions have ':' in the external function name
    // function arguments string format:
    // requiredArg1[,requiredArg2]...[|optionalArg1[,optionalArg2]...]
    darbotdb::aql::Function customScorer(
        "CUSTOMSCORER", ".|+",
        darbotdb::aql::Function::makeFlags(
            darbotdb::aql::Function::Flags::Deterministic,
            darbotdb::aql::Function::Flags::Cacheable,
            darbotdb::aql::Function::Flags::CanRunOnDBServerCluster,
            darbotdb::aql::Function::Flags::CanRunOnDBServerOneShard),
        nullptr);
    darbotdb::iresearch::addFunction(functions, customScorer);

    auto& dbPathFeature = server.getFeature<darbotdb::DatabasePathFeature>();
    darbotdb::tests::setDatabasePath(
        dbPathFeature);  // ensure test data is stored in a unique directory
  }

  TRI_vocbase_t& vocbase() {
    TRI_ASSERT(_vocbase != nullptr);
    return *_vocbase;
  }

  darbotdb::iresearch::LinkVersion linkVersion() const noexcept {
    return std::get<1>(GetParam());
  }

  darbotdb::iresearch::LinkVersion version() const noexcept {
    return std::get<1>(GetParam());
  }
};

constexpr const char* collectionName1 = "collection_1";
constexpr const char* collectionName2 = "collection_2";
constexpr const char* viewName = "view";

class QueryNoMaterialization : public QueryTestMulti {
 protected:
  void addLinkToCollection(
      std::shared_ptr<darbotdb::iresearch::IResearchView>& view) {
    auto versionStr = std::to_string(static_cast<uint32_t>(linkVersion()));

    auto updateJson =
        VPackParser::fromJson(std::string("{") +
                              "\"links\": {"
                              "\"" +
                              collectionName1 +
                              "\": {\"includeAllFields\": true, "
                              "\"storeValues\": \"id\", \"version\": " +
                              versionStr +
                              "},"
                              "\"" +
                              collectionName2 +
                              "\": {\"includeAllFields\": true, "
                              "\"storeValues\": \"id\", \"version\": " +
                              versionStr +
                              "}"
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

  void SetUp() override {
    // add collection_1
    std::shared_ptr<darbotdb::LogicalCollection> logicalCollection1;
    {
      auto collectionJson = VPackParser::fromJson(std::string("{\"name\": \"") +
                                                  collectionName1 + "\"}");
      logicalCollection1 = vocbase().createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection1);
    }

    // add collection_2
    std::shared_ptr<darbotdb::LogicalCollection> logicalCollection2;
    {
      auto collectionJson = VPackParser::fromJson(std::string("{\"name\": \"") +
                                                  collectionName2 + "\"}");
      logicalCollection2 = vocbase().createCollection(collectionJson->slice());
      ASSERT_NE(nullptr, logicalCollection2);
    }

    auto createIndexes = [&](int index, std::string_view addition) {
      bool created = false;
      auto createJson = VPackParser::fromJson(absl::Substitute(
          R"({ "name": "index_$0", "type": "inverted",
               "version": $1, $2
               "includeAllFields": true })",
          index, version(), addition));
      logicalCollection1->createIndex(createJson->slice(), created)
          .waitAndGet();
      ASSERT_TRUE(created);
      created = false;
      logicalCollection2->createIndex(createJson->slice(), created)
          .waitAndGet();
      ASSERT_TRUE(created);
    };

    auto addIndexes = [](auto& view, int index) {
      auto const viewDefinition = absl::Substitute(R"({ "indexes": [
        { "collection": "collection_1", "index": "index_$0"},
        { "collection": "collection_2", "index": "index_$0"}
      ]})",
                                                   index);
      auto updateJson = darbotdb::velocypack::Parser::fromJson(viewDefinition);
      auto r = view.properties(updateJson->slice(), true, true);
      EXPECT_TRUE(r.ok()) << r.errorMessage();
    };

    // create view
    if (type() == ViewType::kArangoSearch) {
      auto createJson =
          VPackParser::fromJson(std::string("{") + "\"name\": \"" + viewName +
                                "\", \
           \"type\": \"arangosearch\", \
           \"primarySort\": [{\"field\": \"value\", \"direction\": \"asc\"}, {\"field\": \"foo\", \"direction\": \"desc\"}, {\"field\": \"boo\", \"direction\": \"desc\"}], \
           \"storedValues\": [{\"fields\":[\"str\"], \"compression\":\"none\"}, [\"value\"], [\"_id\"], [\"str\", \"value\"], [\"exist\"]] \
        }");
      auto view = std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
          vocbase().createView(createJson->slice(), false));
      ASSERT_FALSE(!view);

      // add links to collections
      addLinkToCollection(view);
    } else {
      auto createJson = VPackParser::fromJson(
          "{\"name\": \"view\", \"type\": \"search-alias\" }");
      auto view = std::dynamic_pointer_cast<iresearch::Search>(
          vocbase().createView(createJson->slice(), false));
      ASSERT_FALSE(!view);
      createIndexes(1, R"("primarySort": {"fields": [
                            {"field": "value", "direction": "asc"},
                            {"field": "foo",   "direction": "desc"},
                            {"field": "boo",   "direction": "desc"}]},
                          "storedValues": [{"fields":["str"], "compression":"none"}, ["value"], ["_id"], ["str", "value"], ["exist"]],)");
      addIndexes(*view, 1);
    }
    // create view2
    if (type() == ViewType::kArangoSearch) {
      auto createJson =
          VPackParser::fromJson(std::string("{") + "\"name\": \"" + viewName +
                                "2\", \
           \"type\": \"arangosearch\", \
           \"primarySort\": [{\"field\": \"value\", \"direction\": \"asc\"}], \
           \"storedValues\": [] \
        }");
      auto view2 =
          std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
              vocbase().createView(createJson->slice(), false));
      ASSERT_FALSE(!view2);

      // add links to collections
      addLinkToCollection(view2);
    } else {
      auto createJson = VPackParser::fromJson(
          "{\"name\": \"view2\", \"type\": \"search-alias\" }");
      auto view = std::dynamic_pointer_cast<iresearch::Search>(
          vocbase().createView(createJson->slice(), false));
      ASSERT_FALSE(!view);
      createIndexes(
          2,
          R"("primarySort": {"fields": [{"field": "value", "direction": "asc"}]},
                          "storedValues": [],)");
      addIndexes(*view, 2);
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
            "{\"_key\": \"c0\", \"str\": \"cat\", \"foo\": \"foo0\", "
            "\"value\": 0, \"exist\": \"ex0\"},"
            "{\"_key\": \"c1\", \"str\": \"cat\", \"foo\": \"foo1\", "
            "\"value\": 1},"
            "{\"_key\": \"c2\", \"str\": \"cat\", \"foo\": \"foo2\", "
            "\"value\": 2, \"exist\": \"ex2\"},"
            "{\"_key\": \"c3\", \"str\": \"cat\", \"foo\": \"foo3\", "
            "\"value\": 3}"
            "]");

        auto root = builder->slice();
        ASSERT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection1->name(), doc, opt);
          EXPECT_TRUE(res.ok());
        }
      }

      // insert into collection_2
      {
        auto builder = VPackParser::fromJson(
            "["
            "{\"_key\": \"c_0\", \"str\": \"cat\", \"foo\": \"foo_0\", "
            "\"value\": 10, \"exist\": \"ex_10\"},"
            "{\"_key\": \"c_1\", \"str\": \"cat\", \"foo\": \"foo_1\", "
            "\"value\": 11},"
            "{\"_key\": \"c_2\", \"str\": \"cat\", \"foo\": \"foo_2\", "
            "\"value\": 12, \"exist\": \"ex_12\"},"
            "{\"_key\": \"c_3\", \"str\": \"cat\", \"foo\": \"foo_3\", "
            "\"value\": 13}"
            "]");

        auto root = builder->slice();
        ASSERT_TRUE(root.isArray());

        for (auto doc : darbotdb::velocypack::ArrayIterator(root)) {
          auto res = trx.insert(logicalCollection2->name(), doc, opt);
          EXPECT_TRUE(res.ok());
        }
      }

      EXPECT_TRUE(trx.commit().ok());
      EXPECT_TRUE(
          (darbotdb::tests::executeQuery(vocbase(),
                                         "FOR d IN view SEARCH 1 ==1 OPTIONS "
                                         "{ waitForSync: true } RETURN d")
               .result.ok()));  // commit
      EXPECT_TRUE(
          (darbotdb::tests::executeQuery(vocbase(),
                                         "FOR d IN view2 SEARCH 1 ==1 OPTIONS "
                                         "{ waitForSync: true } RETURN d")
               .result.ok()));  // commit
    }
  }

  void executeAndCheck(std::string const& queryString,
                       std::vector<VPackValue> const& expectedValues,
                       darbotdb::velocypack::ValueLength numOfColumns,
                       std::set<std::pair<ptrdiff_t, size_t>>&& fields) {
    EXPECT_TRUE(darbotdb::tests::assertRules(
        vocbase(), queryString,
        {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

    auto query = darbotdb::aql::Query::create(
        darbotdb::transaction::StandaloneContext::create(
            vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
        darbotdb::aql::QueryString(queryString), nullptr);
    auto const res = query->explain();
    ASSERT_TRUE(res.data);
    auto const explanation = res.data->slice();
    darbotdb::velocypack::ArrayIterator nodes(explanation.get("nodes"));
    auto found = false;
    for (auto const node : nodes) {
      if (node.hasKey("type") && node.get("type").isString() &&
          node.get("type").stringView() == "EnumerateViewNode") {
        EXPECT_TRUE(node.hasKey("noMaterialization") &&
                    node.get("noMaterialization").isBool() &&
                    node.get("noMaterialization").getBool());
        ASSERT_TRUE(node.hasKey("viewValuesVars") &&
                    node.get("viewValuesVars").isArray());
        ASSERT_EQ(numOfColumns, node.get("viewValuesVars").length());
        darbotdb::velocypack::ArrayIterator columnFields(
            node.get("viewValuesVars"));
        for (auto const cf : columnFields) {
          ASSERT_TRUE(cf.isObject());
          if (cf.hasKey("fieldNumber")) {
            auto fieldNumber = cf.get("fieldNumber");
            ASSERT_TRUE(fieldNumber.isNumber<size_t>());
            auto it = fields.find(std::make_pair(
                darbotdb::iresearch::IResearchViewNode::kSortColumnNumber,
                fieldNumber.getNumber<size_t>()));
            ASSERT_TRUE(it != fields.end());
            fields.erase(it);
          } else {
            ASSERT_TRUE(cf.hasKey("columnNumber") &&
                        cf.get("columnNumber").isNumber());
            auto columnNumber = cf.get("columnNumber").getNumber<int>();
            ASSERT_TRUE(cf.hasKey("viewStoredValuesVars") &&
                        cf.get("viewStoredValuesVars").isArray());
            darbotdb::velocypack::ArrayIterator fs(
                cf.get("viewStoredValuesVars"));
            for (auto const f : fs) {
              ASSERT_TRUE(f.hasKey("fieldNumber") &&
                          f.get("fieldNumber").isNumber<size_t>());
              auto it = fields.find(std::make_pair(
                  columnNumber, f.get("fieldNumber").getNumber<size_t>()));
              ASSERT_TRUE(it != fields.end());
              fields.erase(it);
            }
          }
        }
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found);
    EXPECT_TRUE(fields.empty());

    auto queryResult = darbotdb::tests::executeQuery(vocbase(), queryString);
    ASSERT_TRUE(queryResult.result.ok());

    auto result = queryResult.data->slice();
    EXPECT_TRUE(result.isArray());

    darbotdb::velocypack::ArrayIterator resultIt(result);

    ASSERT_EQ(expectedValues.size(), resultIt.size());
    // Check values
    auto expectedValue = expectedValues.begin();
    for (; resultIt.valid(); resultIt.next(), ++expectedValue) {
      auto const actualDoc = resultIt.value();
      auto const resolved = actualDoc.resolveExternals();

      if (resolved.isString()) {
        ASSERT_TRUE(expectedValue->isString());
        darbotdb::velocypack::ValueLength length = 0;
        auto resStr = resolved.getString(length);
        EXPECT_TRUE(memcmp(expectedValue->getCharPtr(), resStr, length) == 0);
      } else {
        ASSERT_TRUE(resolved.isNumber());
        EXPECT_EQ(expectedValue->getInt64(), resolved.getInt());
      }
    }
    EXPECT_EQ(expectedValue, expectedValues.end());
  }
};

// -----------------------------------------------------------------------------
// --SECTION--                                                        test suite
// -----------------------------------------------------------------------------

TEST_P(QueryNoMaterialization, sortColumnPriority) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value IN [1, 2, 11, 12] SORT d.value RETURN d.value";

  std::vector<VPackValue> expectedValues{VPackValue(1), VPackValue(2),
                                         VPackValue(11), VPackValue(12)};

  executeAndCheck(
      queryString, expectedValues, 1,
      {{darbotdb::iresearch::IResearchViewNode::kSortColumnNumber, 0}});
}

TEST_P(QueryNoMaterialization, sortColumnPriorityViewsSubquery) {
  // this checks proper stored variables buffer resizing uring optimization
  auto const queryString =
      std::string("FOR c IN ") + viewName +
      "2 SEARCH c.value IN [1, 2, 11, 12] SORT c.value FOR d IN " + viewName +
      " SEARCH d.value == c.value SORT d.value RETURN d.value";

  std::vector<VPackValue> expectedValues{VPackValue(1), VPackValue(2),
                                         VPackValue(11), VPackValue(12)};

  auto queryResult = darbotdb::tests::executeQuery(vocbase(), queryString);
  ASSERT_TRUE(queryResult.result.ok());

  auto result = queryResult.data->slice();
  EXPECT_TRUE(result.isArray());

  darbotdb::velocypack::ArrayIterator resultIt(result);

  ASSERT_EQ(expectedValues.size(), resultIt.size());
  // Check values
  auto expectedValue = expectedValues.begin();
  for (; resultIt.valid(); resultIt.next(), ++expectedValue) {
    auto const actualDoc = resultIt.value();
    auto const resolved = actualDoc.resolveExternals();

    if (resolved.isString()) {
      ASSERT_TRUE(expectedValue->isString());
      darbotdb::velocypack::ValueLength length = 0;
      auto resStr = resolved.getString(length);
      EXPECT_TRUE(memcmp(expectedValue->getCharPtr(), resStr, length) == 0);
    } else {
      ASSERT_TRUE(resolved.isNumber());
      EXPECT_EQ(expectedValue->getInt64(), resolved.getInt());
    }
  }
  EXPECT_EQ(expectedValue, expectedValues.end());
}

TEST_P(QueryNoMaterialization, maxMatchColumnPriority) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " FILTER d.str == 'cat' SORT d.value RETURN d.value";

  std::vector<VPackValue> expectedValues{
      VPackValue(0),  VPackValue(1),  VPackValue(2),  VPackValue(3),
      VPackValue(10), VPackValue(11), VPackValue(12), VPackValue(13)};

  executeAndCheck(queryString, expectedValues, 1, {{3, 0}, {3, 1}});
}

TEST_P(QueryNoMaterialization, sortAndStoredValues) {
  auto const queryString =
      std::string("FOR d IN ") + viewName + " SORT d._id RETURN d.foo";

  std::vector<VPackValue> expectedValues{
      VPackValue("foo0"),  VPackValue("foo1"),  VPackValue("foo2"),
      VPackValue("foo3"),  VPackValue("foo_0"), VPackValue("foo_1"),
      VPackValue("foo_2"), VPackValue("foo_3")};

  executeAndCheck(
      queryString, expectedValues, 2,
      {{darbotdb::iresearch::IResearchViewNode::kSortColumnNumber, 1}, {2, 0}});
}

TEST_P(QueryNoMaterialization, fieldExistence) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH EXISTS(d.exist) SORT d.value RETURN d.value";

  std::vector<VPackValue> expectedValues{VPackValue(0), VPackValue(2),
                                         VPackValue(10), VPackValue(12)};

  executeAndCheck(
      queryString, expectedValues, 1,
      {{darbotdb::iresearch::IResearchViewNode::kSortColumnNumber, 0}});
}

TEST_P(QueryNoMaterialization, storedFieldExistence) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH EXISTS(d.exist) SORT d.value RETURN d.exist";

  std::vector<VPackValue> expectedValues{VPackValue("ex0"), VPackValue("ex2"),
                                         VPackValue("ex_10"),
                                         VPackValue("ex_12")};

  executeAndCheck(
      queryString, expectedValues, 2,
      {{darbotdb::iresearch::IResearchViewNode::kSortColumnNumber, 0}, {4, 0}});
}

TEST_P(QueryNoMaterialization, emptyField) {
  auto const queryString = std::string("FOR d IN ") + viewName +
                           " SORT d.exist DESC LIMIT 1 RETURN d.exist";

  std::vector<VPackValue> expectedValues{VPackValue("ex2")};

  executeAndCheck(queryString, expectedValues, 1, {{4, 0}});
}

TEST_P(QueryNoMaterialization, testStoredValuesRecord) {
  static std::vector<std::string> const EMPTY;
  auto doc = darbotdb::velocypack::Parser::fromJson(
      "{ \"str\": \"abc\", \"value\": 10 }");
  std::string collectionName("testCollection");
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\":\"" + collectionName + "\"}");
  auto logicalCollection = vocbase().createCollection(collectionJson->slice());
  ASSERT_TRUE(logicalCollection);
  size_t const columnsCount = 6;  // PK + storedValues
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \
        \"id\": 42, \
        \"name\": \"testView\", \
        \"type\": \"arangosearch\", \
        \"storedValues\": [{\"fields\":[\"str\"]}, {\"fields\":[\"foo\"]}, {\"fields\":[\"value\"]},\
                          {\"fields\":[\"_id\"]}, {\"fields\":[\"str\", \"foo\", \"value\"]}] \
      }");
  auto view = std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
      vocbase().createView(viewJson->slice(), false));
  ASSERT_TRUE(view);

  auto updateJson =
      VPackParser::fromJson("{\"links\": {\"" + collectionName +
                            "\": {\"includeAllFields\": true} }}");
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
  EXPECT_TRUE(tmpSlice.isObject() && 1 == tmpSlice.length());

  {
    darbotdb::OperationOptions opt;
    darbotdb::transaction::Methods trx(
        darbotdb::transaction::StandaloneContext::create(
            vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
        EMPTY, {logicalCollection->name()}, EMPTY,
        darbotdb::transaction::Options());
    EXPECT_TRUE(trx.begin().ok());
    auto const res = trx.insert(logicalCollection->name(), doc->slice(), opt);
    EXPECT_TRUE(res.ok());

    EXPECT_TRUE(trx.commit().ok());
    EXPECT_TRUE(darbotdb::iresearch::IResearchLinkHelper::find(
                    *logicalCollection, *view)
                    ->commit()
                    .ok());
  }

  {
    darbotdb::transaction::Methods trx(
        darbotdb::transaction::StandaloneContext::create(
            vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
        EMPTY, EMPTY, EMPTY, darbotdb::transaction::Options());
    EXPECT_TRUE(trx.begin().ok());
    auto link = darbotdb::iresearch::IResearchLinkHelper::find(
        *logicalCollection, *view);
    ASSERT_TRUE(link);
    auto const snapshot = link->snapshot();
    auto const& snapshotReader = snapshot.getDirectoryReader();
    std::string const columns[] = {
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("_id"),
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("foo"),
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("foo") +
            darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            "str" +
            darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            "value",
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("str"),
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("value"),
        "@_PK"};
    for (auto const& segment : snapshotReader) {
      auto col = segment.columns();
      auto doc = segment.docs_iterator();
      ASSERT_TRUE(doc);
      ASSERT_TRUE(doc->next());
      size_t counter = 0;
      while (col->next()) {
        auto const& val = col->value();
        ASSERT_TRUE(counter < columnsCount);
        EXPECT_EQ(columns[counter], val.name());
        if (5 == counter) {  // skip PK
          ++counter;
          continue;
        }
        auto columnReader = segment.column(val.id());
        ASSERT_TRUE(columnReader);
        auto valReader = columnReader->iterator(irs::ColumnHint::kNormal);
        ASSERT_TRUE(valReader);
        auto* value = irs::get<irs::payload>(*valReader);
        ASSERT_TRUE(value);
        ASSERT_EQ(doc->value(), valReader->seek(doc->value()));
        if (1 == counter) {  // foo
          EXPECT_TRUE(irs::IsNull(value->value));
          ++counter;
          continue;
        }
        size_t valueSize = value->value.size();
        auto slice = VPackSlice(value->value.data());
        switch (counter) {
          case 0: {
            ASSERT_TRUE(slice.isString());
            darbotdb::velocypack::ValueLength length = 0;
            auto str = slice.getString(length);
            std::string strVal(str, length);
            ASSERT_TRUE(length > collectionName.size());
            EXPECT_EQ(collectionName + "/",
                      strVal.substr(0, collectionName.size() + 1));
            break;
          }
          case 2: {
            darbotdb::velocypack::ValueLength size = slice.byteSize();
            ASSERT_TRUE(slice.isString());
            darbotdb::velocypack::ValueLength length = 0;
            auto str = slice.getString(length);
            EXPECT_EQ("abc", std::string(str, length));
            slice = VPackSlice(slice.start() + slice.byteSize());
            size += slice.byteSize();
            EXPECT_TRUE(slice.isNull());
            slice = VPackSlice(slice.start() + slice.byteSize());
            size += slice.byteSize();
            ASSERT_TRUE(slice.isNumber());
            EXPECT_EQ(10, slice.getNumber<int>());
            EXPECT_EQ(valueSize, size);
            break;
          }
          case 3: {
            ASSERT_TRUE(slice.isString());
            darbotdb::velocypack::ValueLength length = 0;
            auto str = slice.getString(length);
            EXPECT_EQ("abc", std::string(str, length));
            break;
          }
          case 4:
            ASSERT_TRUE(slice.isNumber());
            EXPECT_EQ(10, slice.getNumber<int>());
            break;
          default:
            ASSERT_TRUE(false);
            break;
        }
        ++counter;
      }
      EXPECT_EQ(columnsCount, counter);
    }
  }
}

TEST_P(QueryNoMaterialization, testStoredValuesRecordWithCompression) {
  static std::vector<std::string> const EMPTY;
  auto doc = darbotdb::velocypack::Parser::fromJson(
      "{ \"str\": \"abc\", \"value\": 10 }");
  std::string collectionName("testCollection");
  auto collectionJson = darbotdb::velocypack::Parser::fromJson(
      "{ \"name\":\"" + collectionName + "\"}");
  auto logicalCollection = vocbase().createCollection(collectionJson->slice());
  ASSERT_TRUE(logicalCollection);
  size_t const columnsCount = 6;  // PK + storedValues
  auto viewJson = darbotdb::velocypack::Parser::fromJson(
      "{ \
        \"id\": 42, \
        \"name\": \"testView\", \
        \"type\": \"arangosearch\", \
        \"storedValues\": [{\"fields\":[\"str\"], \"compression\":\"none\"}, [\"foo\"],\
        {\"fields\":[\"value\"], \"compression\":\"lz4\"}, [\"_id\"], {\"fields\":[\"str\", \"foo\", \"value\"]}] \
      }");
  auto view = std::dynamic_pointer_cast<darbotdb::iresearch::IResearchView>(
      vocbase().createView(viewJson->slice(), false));
  ASSERT_TRUE(view);

  auto updateJson =
      VPackParser::fromJson("{\"links\": {\"" + collectionName +
                            "\": {\"includeAllFields\": true} }}");
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
  EXPECT_TRUE(tmpSlice.isObject() && 1 == tmpSlice.length());

  {
    darbotdb::OperationOptions opt;
    darbotdb::transaction::Methods trx(
        darbotdb::transaction::StandaloneContext::create(
            vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
        EMPTY, {logicalCollection->name()}, EMPTY,
        darbotdb::transaction::Options());
    EXPECT_TRUE(trx.begin().ok());
    auto const res = trx.insert(logicalCollection->name(), doc->slice(), opt);
    EXPECT_TRUE(res.ok());

    EXPECT_TRUE(trx.commit().ok());
    EXPECT_TRUE(darbotdb::iresearch::IResearchLinkHelper::find(
                    *logicalCollection, *view)
                    ->commit()
                    .ok());
  }

  {
    darbotdb::transaction::Methods trx(
        darbotdb::transaction::StandaloneContext::create(
            vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
        EMPTY, EMPTY, EMPTY, darbotdb::transaction::Options());
    EXPECT_TRUE(trx.begin().ok());
    auto link = darbotdb::iresearch::IResearchLinkHelper::find(
        *logicalCollection, *view);
    ASSERT_TRUE(link);
    auto const snapshot = link->snapshot();
    auto const& snapshotReader = snapshot.getDirectoryReader();
    std::string const columns[] = {
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("_id"),
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("foo"),
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("foo") +
            darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            "str" +
            darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            "value",
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("str"),
        darbotdb::iresearch::IResearchViewStoredValues::FIELDS_DELIMITER +
            std::string("value"),
        "@_PK"};
    for (auto const& segment : snapshotReader) {
      auto col = segment.columns();
      auto doc = segment.docs_iterator();
      ASSERT_TRUE(doc);
      ASSERT_TRUE(doc->next());
      size_t counter = 0;
      while (col->next()) {
        auto const& val = col->value();
        ASSERT_TRUE(counter < columnsCount);
        EXPECT_EQ(columns[counter], val.name());
        if (5 == counter) {  // skip PK
          ++counter;
          continue;
        }
        auto columnReader = segment.column(val.id());
        ASSERT_TRUE(columnReader);
        auto valReader = columnReader->iterator(irs::ColumnHint::kNormal);
        ASSERT_TRUE(valReader);
        auto* value = irs::get<irs::payload>(*valReader);
        ASSERT_TRUE(value);
        ASSERT_EQ(doc->value(), valReader->seek(doc->value()));
        if (1 == counter) {  // foo
          EXPECT_TRUE(irs::IsNull(value->value));
          ++counter;
          continue;
        }
        size_t valueSize = value->value.size();
        auto slice = VPackSlice(value->value.data());
        switch (counter) {
          case 0: {
            ASSERT_TRUE(slice.isString());
            darbotdb::velocypack::ValueLength length = 0;
            auto str = slice.getString(length);
            std::string strVal(str, length);
            ASSERT_TRUE(length > collectionName.size());
            EXPECT_EQ(collectionName + "/",
                      strVal.substr(0, collectionName.size() + 1));
            break;
          }
          case 2: {
            darbotdb::velocypack::ValueLength size = slice.byteSize();
            ASSERT_TRUE(slice.isString());
            darbotdb::velocypack::ValueLength length = 0;
            auto str = slice.getString(length);
            EXPECT_EQ("abc", std::string(str, length));
            slice = VPackSlice(slice.start() + slice.byteSize());
            size += slice.byteSize();
            EXPECT_TRUE(slice.isNull());
            slice = VPackSlice(slice.start() + slice.byteSize());
            size += slice.byteSize();
            ASSERT_TRUE(slice.isNumber());
            EXPECT_EQ(10, slice.getNumber<int>());
            EXPECT_EQ(valueSize, size);
            break;
          }
          case 3: {
            ASSERT_TRUE(slice.isString());
            darbotdb::velocypack::ValueLength length = 0;
            auto str = slice.getString(length);
            EXPECT_EQ("abc", std::string(str, length));
            break;
          }
          case 4:
            ASSERT_TRUE(slice.isNumber());
            EXPECT_EQ(10, slice.getNumber<int>());
            break;
          default:
            ASSERT_TRUE(false);
            break;
        }
        ++counter;
      }
      EXPECT_EQ(columnsCount, counter);
    }
  }
}

TEST_P(QueryNoMaterialization, matchSortButNotEnoughAttributes) {
  auto const queryString =
      std::string("FOR d IN ") + viewName +
      " SEARCH d.value IN [1, 2, 11, 12] FILTER d.boo == '12312' SORT d.boo "
      "ASC "
      " RETURN DISTINCT  {resource_type: d.foo, version: d.not_in_stored}";

  std::vector<VPackValue> expectedValues{};
  EXPECT_TRUE(darbotdb::tests::assertRules(
      vocbase(), queryString,
      {darbotdb::aql::OptimizerRule::handleArangoSearchViewsRule}));

  auto query = darbotdb::aql::Query::create(
      darbotdb::transaction::StandaloneContext::create(
          vocbase(), darbotdb::transaction::OperationOriginTestCase{}),
      darbotdb::aql::QueryString(queryString), nullptr);
  auto const res = query->explain();  // this should not crash!
  ASSERT_TRUE(res.data);
  auto const explanation = res.data->slice();
  darbotdb::velocypack::ArrayIterator nodes(explanation.get("nodes"));
  auto found = false;
  for (auto const node : nodes) {
    if (node.hasKey("type") && node.get("type").isString() &&
        node.get("type").stringView() == "EnumerateViewNode") {
      EXPECT_FALSE(node.hasKey("noMaterialization"));
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

INSTANTIATE_TEST_CASE_P(
    IResearch, QueryNoMaterialization,
    testing::Values(std::tuple{ViewType::kArangoSearch,
                               darbotdb::iresearch::LinkVersion::MIN},
                    std::tuple{ViewType::kArangoSearch,
                               darbotdb::iresearch::LinkVersion::MAX},
                    std::tuple{ViewType::kSearchAlias,
                               darbotdb::iresearch::LinkVersion::MAX}));

}  // namespace
}  // namespace darbotdb::tests
