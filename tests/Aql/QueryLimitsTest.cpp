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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "Aql/Ast.h"
#include "Aql/ExecutionPlan.h"
#include "Aql/Query.h"
#include "Aql/QueryString.h"
#include "Aql/SharedQueryState.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/ExecContext.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/VocbaseInfo.h"
#include "VocBase/vocbase.h"

#include <velocypack/Builder.h>
#include <velocypack/Parser.h>
#include <velocypack/Slice.h>

#include "gtest/gtest.h"
#include "Mocks/Servers.h"

namespace {

class AqlQueryLimitsTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 protected:
  darbotdb::tests::mocks::MockAqlServer server;

 public:
  AqlQueryLimitsTest() : server(false) { server.startFeatures(); }

  darbotdb::aql::QueryResult executeQuery(
      TRI_vocbase_t& vocbase, std::string const& queryString,
      std::shared_ptr<darbotdb::velocypack::Builder> bindVars = nullptr,
      std::string const& optionsString = "{}") {
    auto ctx = std::make_shared<darbotdb::transaction::StandaloneContext>(
        vocbase, darbotdb::transaction::OperationOriginTestCase{});
    auto query = darbotdb::aql::Query::create(
        ctx, darbotdb::aql::QueryString(queryString), bindVars,
        darbotdb::aql::QueryOptions(
            darbotdb::velocypack::Parser::fromJson(optionsString)->slice()));

    return query->executeSync();
  }
};

TEST_F(AqlQueryLimitsTest, testManyNodes) {
  darbotdb::CreateDatabaseInfo testDBInfo(server.server(),
                                          darbotdb::ExecContext::current());
  testDBInfo.load("testVocbase", 2);
  TRI_vocbase_t vocbase(std::move(testDBInfo));

  std::string query("LET x = NOOPT('testi')\n");
  size_t cnt = darbotdb::aql::ExecutionPlan::maxPlanNodes -
               4;  // singleton + calculation + calculation + return
  for (size_t i = 1; i <= cnt; ++i) {
    query.append("FILTER x\n");
  }
  query.append("RETURN 1");

  auto queryResult = executeQuery(vocbase, query);

  ASSERT_TRUE(queryResult.result.ok());
  auto slice = queryResult.data->slice();
  EXPECT_TRUE(slice.isArray());
  EXPECT_EQ(1, slice.length());
  EXPECT_EQ(1, slice[0].getNumber<int64_t>());
}

TEST_F(AqlQueryLimitsTest, testTooManyNodes) {
  darbotdb::CreateDatabaseInfo testDBInfo(server.server(),
                                          darbotdb::ExecContext::current());
  testDBInfo.load("testVocbase", 2);
  TRI_vocbase_t vocbase(std::move(testDBInfo));

  std::string query("LET x = NOOPT('testi')\n");
  size_t cnt = darbotdb::aql::ExecutionPlan::maxPlanNodes;
  for (size_t i = 1; i <= cnt; ++i) {
    query.append("FILTER x\n");
  }
  query.append("RETURN 1");

  auto queryResult = executeQuery(vocbase, query);

  ASSERT_FALSE(queryResult.result.ok());
  ASSERT_EQ(TRI_ERROR_QUERY_TOO_MUCH_NESTING, queryResult.result.errorNumber());
}

TEST_F(AqlQueryLimitsTest, testDeepRecursion) {
  darbotdb::CreateDatabaseInfo testDBInfo(server.server(),
                                          darbotdb::ExecContext::current());
  testDBInfo.load("testVocbase", 2);
  TRI_vocbase_t vocbase(std::move(testDBInfo));

  std::string query("RETURN 0");
  size_t cnt = darbotdb::aql::Ast::maxExpressionNesting - 2;
  for (size_t i = 1; i <= cnt; ++i) {
    query.append(" + ");
    query.append(std::to_string(i));
  }

  auto queryResult = executeQuery(vocbase, query);

  ASSERT_TRUE(queryResult.result.ok());
  auto slice = queryResult.data->slice();
  EXPECT_TRUE(slice.isArray());
  EXPECT_EQ(1, slice.length());
  EXPECT_EQ(124251, slice[0].getNumber<int64_t>());
}

TEST_F(AqlQueryLimitsTest, testTooDeepRecursion) {
  darbotdb::CreateDatabaseInfo testDBInfo(server.server(),
                                          darbotdb::ExecContext::current());
  testDBInfo.load("testVocbase", 2);
  TRI_vocbase_t vocbase(std::move(testDBInfo));

  std::string query("RETURN 0");
  size_t cnt = darbotdb::aql::Ast::maxExpressionNesting;
  for (size_t i = 1; i <= cnt; ++i) {
    query.append(" + ");
    query.append(std::to_string(i));
  }

  auto queryResult = executeQuery(vocbase, query);
  ASSERT_FALSE(queryResult.result.ok());
  ASSERT_EQ(TRI_ERROR_QUERY_TOO_MUCH_NESTING, queryResult.result.errorNumber());
}

}  // namespace
