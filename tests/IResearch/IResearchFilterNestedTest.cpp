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
////////////////////////////////////////////////////////////////////////////////

#include "gtest/gtest.h"

#include "search/boolean_filter.hpp"
#include "search/column_existence_filter.hpp"
#include "search/nested_filter.hpp"
#include "search/term_filter.hpp"

#include "Mocks/LogLevels.h"
#include "Mocks/Servers.h"
#include "Mocks/StorageEngineMock.h"
#include "Aql/AqlFunctionFeature.h"
#include "Aql/Function.h"
#include "IResearch/common.h"
#include "IResearch/IResearchCommon.h"
#include "IResearch/IResearchFilterFactoryCommon.h"
#include "IResearch/ExpressionContextMock.h"
#include "RestServer/DatabaseFeature.h"
#include "VocBase/Methods/Collections.h"

class IResearchFilterNestedTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 protected:
  darbotdb::tests::mocks::MockAqlServer server;

 private:
  TRI_vocbase_t* _vocbase;

 protected:
  IResearchFilterNestedTest() {
    darbotdb::tests::init();

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

    auto& analyzers =
        server.getFeature<darbotdb::iresearch::IResearchAnalyzerFeature>();
    darbotdb::iresearch::IResearchAnalyzerFeature::EmplaceResult result;

    auto& dbFeature = server.getFeature<darbotdb::DatabaseFeature>();
    dbFeature.createDatabase(
        testDBInfo(server.server()),
        _vocbase);  // required for IResearchAnalyzerFeature::emplace(...)
    std::shared_ptr<darbotdb::LogicalCollection> unused;
    darbotdb::OperationOptions options(darbotdb::ExecContext::current());
    darbotdb::methods::Collections::createSystem(
        *_vocbase, options, darbotdb::tests::AnalyzerCollectionName, false,
        unused);
    analyzers.emplace(
        result, "testVocbase::test_analyzer", "TestAnalyzer",
        darbotdb::velocypack::Parser::fromJson("{ \"args\": \"abc\"}")->slice(),
        darbotdb::transaction::OperationOriginTestCase{});  // cache analyzer
  }

  TRI_vocbase_t& vocbase() { return *_vocbase; }
};

// TODO Add community only tests (byExpression)

#if USE_ENTERPRISE
#include "tests/IResearch/IResearchFilterNestedTestEE.h"
#endif
