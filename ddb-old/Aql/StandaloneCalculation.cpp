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

#include "StandaloneCalculation.h"

#include "Aql/Ast.h"
#include "Aql/AqlFunctionFeature.h"
#include "Aql/AqlTransaction.h"
#include "Aql/ExpressionContext.h"
#include "Aql/Expression.h"
#include "Aql/LazyConditions.h"
#include "Aql/Optimizer.h"
#include "Aql/OptimizerRule.h"
#include "Aql/Parser.h"
#include "Aql/QueryContext.h"
#include "Aql/QueryString.h"
#include "Basics/Exceptions.h"
#include "Basics/debugging.h"
#include "StorageEngine/TransactionState.h"
#include "Transaction/SmartContext.h"
#include "Transaction/Status.h"
#include "Utils/CollectionNameResolver.h"
#include "VocBase/Identifiers/DataSourceId.h"
#include "VocBase/vocbase.h"

#include <absl/strings/str_cat.h>

using namespace darbotdb;
using namespace darbotdb::aql;

namespace {

/// @brief Dummy transaction state which does nothing but provides valid
/// statuses to keep ASSERT happy
class CalculationTransactionState final : public darbotdb::TransactionState {
 public:
  explicit CalculationTransactionState(TRI_vocbase_t& vocbase,
                                       transaction::OperationOrigin trxType)
      : TransactionState(vocbase, darbotdb::TransactionId(0),
                         darbotdb::transaction::Options(), trxType) {
    updateStatus(darbotdb::transaction::Status::RUNNING);  // always running to
                                                           // make ASSERTS happy
  }

  ~CalculationTransactionState() override {
    if (status() == darbotdb::transaction::Status::RUNNING) {
      updateStatus(
          darbotdb::transaction::Status::ABORTED);  // simulate state changes to
                                                    // make ASSERTS happy
    }
  }

  [[nodiscard]] bool ensureSnapshot() override { return false; }

  /// @brief begin a transaction
  [[nodiscard]] futures::Future<Result> beginTransaction(
      darbotdb::transaction::Hints) override {
    return Result{};
  }

  /// @brief commit a transaction
  [[nodiscard]] futures::Future<darbotdb::Result> commitTransaction(
      darbotdb::transaction::Methods*) override {
    applyBeforeCommitCallbacks();
    updateStatus(
        darbotdb::transaction::Status::COMMITTED);  // simulate state changes to
                                                    // make ASSERTS happy
    applyAfterCommitCallbacks();
    return Result{};
  }

  /// @brief abort a transaction
  [[nodiscard]] darbotdb::Result abortTransaction(
      darbotdb::transaction::Methods*) override {
    updateStatus(
        darbotdb::transaction::Status::ABORTED);  // simulate state changes to
                                                  // make ASSERTS happy
    return {};
  }

  Result triggerIntermediateCommit() override {
    ADB_PROD_ASSERT(false) << "triggerIntermediateCommit is not supported in "
                              "CalculationTransactionState";
    return Result{TRI_ERROR_INTERNAL};
  }

  [[nodiscard]] futures::Future<Result> performIntermediateCommitIfRequired(
      darbotdb::DataSourceId collectionId) override {
    // Analyzers do not write. so do nothing
    return Result{};
  }

  [[nodiscard]] uint64_t numPrimitiveOperations() const noexcept override {
    return 0;
  }

  [[nodiscard]] bool hasFailedOperations() const noexcept override {
    return false;
  }

  /// @brief number of commits, including intermediate commits
  [[nodiscard]] uint64_t numCommits() const noexcept override { return 0; }

  /// @brief number of intermediate commits
  [[nodiscard]] uint64_t numIntermediateCommits() const noexcept override {
    return 0;
  }

  void addIntermediateCommits(uint64_t /*value*/) override {
    TRI_ASSERT(false);
    THROW_ddb_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
  }

  [[nodiscard]] TRI_voc_tick_t lastOperationTick() const noexcept override {
    return 0;
  }

  std::unique_ptr<darbotdb::TransactionCollection> createTransactionCollection(
      darbotdb::DataSourceId cid,
      darbotdb::AccessMode::Type accessType) override {
    TRI_ASSERT(false);
    THROW_ddb_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
  }
};

/// @brief Dummy transaction context which just gives dummy state
struct CalculationTransactionContext final
    : public darbotdb::transaction::SmartContext {
  explicit CalculationTransactionContext(
      TRI_vocbase_t& vocbase, transaction::OperationOrigin operationOrigin)
      : SmartContext(vocbase,
                     darbotdb::transaction::Context::makeTransactionId(),
                     nullptr, operationOrigin),
        _calculationTransactionState(vocbase, operationOrigin) {}

  /// @brief get transaction state, determine commit responsiblity
  std::shared_ptr<darbotdb::TransactionState> acquireState(
      darbotdb::transaction::Options const& options,
      bool& responsibleForCommit) override {
    return {std::shared_ptr<darbotdb::TransactionState>(),
            &_calculationTransactionState};
  }

  /// @brief unregister the transaction
  void unregisterTransaction() noexcept override {}

  std::shared_ptr<Context> clone() const override {
    TRI_ASSERT(false);
    THROW_ddb_EXCEPTION_MESSAGE(
        TRI_ERROR_NOT_IMPLEMENTED,
        "CalculationTransactionContext cloning is not implemented");
  }

 private:
  CalculationTransactionState _calculationTransactionState;
};

class CalculationQueryContext final : public darbotdb::aql::QueryContext {
 public:
  explicit CalculationQueryContext(TRI_vocbase_t& vocbase,
                                   transaction::OperationOrigin operationOrigin)
      : QueryContext(vocbase, operationOrigin),
        _resolver(vocbase),
        _transactionContext(vocbase, operationOrigin) {
    _ast = std::make_unique<Ast>(*this, NON_CONST_PARAMETERS);
    _trx = AqlTransaction::create(newTrxContext(), _collections,
                                  _queryOptions.transactionOptions,
                                  std::unordered_set<std::string>{});
    _trx->addHint(darbotdb::transaction::Hints::Hint::FROM_TOPLEVEL_AQL);
    _trx->addHint(darbotdb::transaction::Hints::Hint::
                      SINGLE_OPERATION);  // to avoid taking db snapshot
    auto res = _trx->begin();
    if (res.fail()) {
      throw basics::Exception(std::move(res));
    }
  }

  darbotdb::aql::QueryOptions const& queryOptions() const override {
    return _queryOptions;
  }

  darbotdb::aql::QueryOptions& queryOptions() noexcept override {
    return _queryOptions;
  }

  double getLockTimeout() const noexcept override {
    return _queryOptions.transactionOptions.lockTimeout;
  }

  void setLockTimeout(double timeout) noexcept override {
    _queryOptions.transactionOptions.lockTimeout = timeout;
  }

  /// @brief pass-thru a resolver object from the transaction context
  darbotdb::CollectionNameResolver const& resolver() const override {
    return _resolver;
  }

  darbotdb::velocypack::Options const& vpackOptions() const override {
    return darbotdb::velocypack::Options::Defaults;
  }

  /// @brief create a transaction::Context
  std::shared_ptr<darbotdb::transaction::Context> newTrxContext()
      const override {
    return std::shared_ptr<darbotdb::transaction::Context>(
        std::shared_ptr<darbotdb::transaction::Context>(),
        &_transactionContext);
  }

  darbotdb::transaction::Methods& trxForOptimization() override {
    return *_trx;
  }

  bool killed() const override { return false; }

  void debugKillQuery() override {}

  /// @brief whether or not a query is a modification query
  bool isModificationQuery() const noexcept override { return false; }

  bool isAsyncQuery() const noexcept override { return false; }

  void enterV8Executor() override {
    TRI_ASSERT(false);
    THROW_ddb_EXCEPTION_MESSAGE(
        TRI_ERROR_NOT_IMPLEMENTED,
        "CalculationQueryContext: entering V8 executor is not implemented");
  }

 private:
  darbotdb::aql::QueryOptions _queryOptions;
  darbotdb::CollectionNameResolver _resolver;
  mutable CalculationTransactionContext _transactionContext;
  std::unique_ptr<darbotdb::transaction::Methods> _trx;
};

}  // namespace

namespace darbotdb::aql {

std::unique_ptr<QueryContext> StandaloneCalculation::buildQueryContext(
    TRI_vocbase_t& vocbase, transaction::OperationOrigin operationOrigin) {
  return std::make_unique<::CalculationQueryContext>(vocbase, operationOrigin);
}

Result StandaloneCalculation::validateQuery(
    TRI_vocbase_t& vocbase, std::string_view queryString,
    std::string_view parameterName, std::string_view errorContext,
    transaction::OperationOrigin operationOrigin, bool isComputedValue) {
  try {
    CalculationQueryContext queryContext(vocbase, operationOrigin);
    auto ast = queryContext.ast();
    TRI_ASSERT(ast);
    auto qs = darbotdb::aql::QueryString(queryString);
    Parser parser(queryContext, *ast, qs);
    if (isComputedValue) {
      // force the condition of the ternary operator (condition ? truePart :
      // falsePart) to be always inlined and not be extracted into its own LET
      // node. if we don't set this boolean flag here, then a ternary operator
      // could create additional LET nodes, which is not supported inside
      // computed values.
      parser.lazyConditions().pushForceInline();
    }
    parser.parse();
    ast->validateAndOptimize(
        queryContext.trxForOptimization(),
        {.optimizeNonCacheable = false, .optimizeFunctionCalls = false});
    AstNode* astRoot = const_cast<AstNode*>(ast->root());
    TRI_ASSERT(astRoot);
    TRI_ASSERT(astRoot->type == NODE_TYPE_ROOT);

    // Forbid all V8 related stuff as it is not available on DBServers where
    // analyzers run.
    if (astRoot->willUseV8()) {
      return {TRI_ERROR_BAD_PARAMETER,
              absl::StrCat("V8 usage is forbidden", errorContext)};
    }

    // no modification (as data access is forbidden) but to give more clear
    // error message
    if (ast->containsModificationNode()) {
      return {TRI_ERROR_BAD_PARAMETER,
              absl::StrCat("DML is forbidden", errorContext)};
    }

    // no traversal (also data access is forbidden) but to give more clear error
    // message
    if (ast->containsTraversal()) {
      return {TRI_ERROR_BAD_PARAMETER,
              absl::StrCat("Traversal usage is forbidden", errorContext)};
    }

    std::string errorMessage;

    // Forbid to use functions that reference analyzers -> problems on recovery
    // as analyzers are not available for querying. Forbid all non-Dbserver
    // runnable functions as it is not available on DBServers where analyzers
    // run.
    darbotdb::aql::Ast::traverseReadOnly(
        astRoot,
        [&errorMessage, &parameterName, &errorContext,
         isComputedValue](darbotdb::aql::AstNode const* node) -> bool {
          TRI_ASSERT(node);
          switch (node->type) {
            case darbotdb::aql::NODE_TYPE_SUBQUERY:
            case darbotdb::aql::NODE_TYPE_FOR:
            case darbotdb::aql::NODE_TYPE_LET: {
              // these nodes are only ok for analyzer expressions, but
              // not for computed values
              if (isComputedValue) {
                errorMessage =
                    absl::StrCat("Node type '", node->getTypeString(),
                                 "' is forbidden", std::string{errorContext});
                return false;
              }
            }
            // fall-through intentional

            // these nodes are ok unconditionally
            case darbotdb::aql::NODE_TYPE_ROOT:
            case darbotdb::aql::NODE_TYPE_FILTER:
            case darbotdb::aql::NODE_TYPE_ARRAY:
            case darbotdb::aql::NODE_TYPE_ARRAY_FILTER:
            case darbotdb::aql::NODE_TYPE_RETURN:
            case darbotdb::aql::NODE_TYPE_SORT:
            case darbotdb::aql::NODE_TYPE_SORT_ELEMENT:
            case darbotdb::aql::NODE_TYPE_LIMIT:
            case darbotdb::aql::NODE_TYPE_VARIABLE:
            case darbotdb::aql::NODE_TYPE_ASSIGN:
            case darbotdb::aql::NODE_TYPE_OPERATOR_UNARY_PLUS:
            case darbotdb::aql::NODE_TYPE_OPERATOR_UNARY_MINUS:
            case darbotdb::aql::NODE_TYPE_OPERATOR_UNARY_NOT:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_AND:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_OR:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_PLUS:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_MINUS:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_TIMES:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_DIV:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_MOD:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_NE:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_GE:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_IN:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_NIN:
            case darbotdb::aql::NODE_TYPE_OPERATOR_TERNARY:
            case darbotdb::aql::NODE_TYPE_EXPANSION:
            case darbotdb::aql::NODE_TYPE_ITERATOR:
            case darbotdb::aql::NODE_TYPE_VALUE:
            case darbotdb::aql::NODE_TYPE_OBJECT:
            case darbotdb::aql::NODE_TYPE_OBJECT_ELEMENT:
            case darbotdb::aql::NODE_TYPE_REFERENCE:
            case darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS:
            case darbotdb::aql::NODE_TYPE_BOUND_ATTRIBUTE_ACCESS:
            case darbotdb::aql::NODE_TYPE_RANGE:
            case darbotdb::aql::NODE_TYPE_NOP:
            case darbotdb::aql::NODE_TYPE_CALCULATED_OBJECT_ELEMENT:
            case darbotdb::aql::NODE_TYPE_PASSTHRU:
            case darbotdb::aql::NODE_TYPE_ARRAY_LIMIT:
            case darbotdb::aql::NODE_TYPE_DISTINCT:
            case darbotdb::aql::NODE_TYPE_OPERATOR_NARY_AND:
            case darbotdb::aql::NODE_TYPE_OPERATOR_NARY_OR:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_EQ:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_NE:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_LT:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_LE:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_GT:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_GE:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_IN:
            case darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_ARRAY_NIN:
            case darbotdb::aql::NODE_TYPE_QUANTIFIER:
              break;
            // some nodes are ok with restrictions
            case darbotdb::aql::NODE_TYPE_FCALL: {
              auto func =
                  static_cast<darbotdb::aql::Function*>(node->getData());
              if (!func->hasFlag(darbotdb::aql::Function::Flags::
                                     CanRunOnDBServerCluster) ||
                  !func->hasFlag(darbotdb::aql::Function::Flags::
                                     CanRunOnDBServerOneShard) ||
                  func->hasFlag(darbotdb::aql::Function::Flags::Internal) ||
                  func->hasFlag(
                      darbotdb::aql::Function::Flags::CanReadDocuments) ||
                  !func->hasFlag(
                      darbotdb::aql::Function::Flags::CanUseInAnalyzer)) {
                errorMessage = absl::StrCat("Function '", func->name,
                                            " is forbidden", errorContext);
                return false;
              }
            } break;
            case darbotdb::aql::NODE_TYPE_PARAMETER: {
              if (node->getStringView() != parameterName) {
                errorMessage = absl::StrCat("Invalid bind parameter '",
                                            node->getStringView(), "' found");
                return false;
              }
            } break;
            // by default everything else is forbidden
            default:
              errorMessage = absl::StrCat("Node type '", node->getTypeString(),
                                          "' is forbidden", errorContext);
              return false;
          }
          return true;
        });

    if (errorMessage.empty() && isComputedValue &&
        (astRoot->numMembers() != 1 ||
         astRoot->getMember(0)->type != NODE_TYPE_RETURN)) {
      // computed values expressions must start with a RETURN statement
      return {
          TRI_ERROR_BAD_PARAMETER,
          absl::StrCat(
              "Computation expression needs to start with a RETURN statement",
              errorContext)};
    }

    if (!errorMessage.empty()) {
      return {TRI_ERROR_BAD_PARAMETER, errorMessage};
    }
  } catch (darbotdb::basics::Exception const& e) {
    return {TRI_ERROR_QUERY_PARSE, absl::StrCat(e.message(), errorContext)};
  } catch (std::exception const& e) {
    return {TRI_ERROR_QUERY_PARSE, e.what()};
  } catch (...) {
    TRI_ASSERT(false);
    return {TRI_ERROR_QUERY_PARSE, "Unexpected"};
  }
  return {};
}

}  // namespace darbotdb::aql
