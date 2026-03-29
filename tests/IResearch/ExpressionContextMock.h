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

#pragma once

#include "Aql/QueryContext.h"
#include "Aql/AqlFunctionsInternalCache.h"
#include "IResearch/IResearchExpressionContext.h"
#include "Transaction/Methods.h"

#include <unordered_map>

struct TRI_vocbase_t;

namespace darbotdb {
namespace aql {

struct Variable;  // forward decl

}  // namespace aql
}  // namespace darbotdb

struct ExpressionContextMock final
    : public darbotdb::iresearch::ViewExpressionContextBase {
  static ExpressionContextMock EMPTY;

  ExpressionContextMock()
      : ViewExpressionContextBase(nullptr, nullptr, nullptr) {}

  ~ExpressionContextMock();

  darbotdb::aql::AqlValue getVariableValue(
      darbotdb::aql::Variable const* variable, bool doCopy,
      bool& mustDestroy) const override;

  void setVariable(darbotdb::aql::Variable const* variable,
                   darbotdb::velocypack::Slice value) override {
    // do nothing
  }

  void clearVariable(
      darbotdb::aql::Variable const* variable) noexcept override {
    // do nothing
  }

  void setTrx(darbotdb::transaction::Methods* trx) { this->_trx = trx; }

  darbotdb::aql::AqlFunctionsInternalCache _regexCache;
  std::unordered_map<std::string, darbotdb::aql::AqlValue> vars;
};  // ExpressionContextMock
