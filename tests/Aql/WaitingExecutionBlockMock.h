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
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Aql/ExecutionBlock.h"
#include "Aql/ExecutionState.h"
#include "Aql/Executor/ScatterExecutor.h"

#include <velocypack/Builder.h>

namespace darbotdb {
namespace aql {
class AqlItemBlock;
class ExecutionEngine;
class ExecutionNode;
class SkipResult;
}  // namespace aql

namespace tests {
namespace aql {

/**
 * @brief A Execution block that simulates the WAITING, HASMORE, DONE API.
 */
class WaitingExecutionBlockMock final : public darbotdb::aql::ExecutionBlock {
 public:
  /**
   * @brief Define how often this Block should return "WAITING"
   */
  enum WaitingBehaviour {
    NEVER,  // Never return WAITING
    ONCE,   // Return WAITING on the first execute call, afterwards return all
            // blocks
    ALWAYS  // Return WAITING once for every execute Call.
  };

  using WakeupCallback = std::function<void()>;
  using ExecuteCallback = std::function<void()>;

  /**
   * @brief Create a WAITING ExecutionBlockMock
   *
   * @param engine Required by API.
   * @param node Required by API.
   * @param data Must be a shared_ptr to an VPackArray.
   * @param variant The waiting behaviour of this block (default ALWAYS), see
   * WaitingBehaviour
   */
  WaitingExecutionBlockMock(
      darbotdb::aql::ExecutionEngine* engine,
      darbotdb::aql::ExecutionNode const* node,
      std::deque<darbotdb::aql::SharedAqlItemBlockPtr>&& data,
      WaitingBehaviour variant, size_t subqueryDepth = 0,
      WakeupCallback wakeUpCallback = {}, ExecuteCallback executeCallback = {});

  /**
   * @brief Initialize the cursor. Return values will be alternating.
   *
   * @param items Will be ignored
   * @param pos Will be ignored
   *
   * @return First <WAITING, TRI_ERROR_NO_ERROR>
   *         Second <DONE, TRI_ERROR_NO_ERROR>
   */
  std::pair<darbotdb::aql::ExecutionState, darbotdb::Result> initializeCursor(
      darbotdb::aql::InputAqlItemRow const& input) override;

  std::tuple<darbotdb::aql::ExecutionState, darbotdb::aql::SkipResult,
             darbotdb::aql::SharedAqlItemBlockPtr>
  execute(darbotdb::aql::AqlCallStack const& stack) override;

  auto remainingRows() const -> uint64_t;

  auto getLastCall() const -> darbotdb::aql::AqlCall;

 private:
  // Implementation of execute
  std::tuple<darbotdb::aql::ExecutionState, darbotdb::aql::SkipResult,
             darbotdb::aql::SharedAqlItemBlockPtr>
  executeWithoutTrace(darbotdb::aql::AqlCallStack stack);

  void wakeupCallback();
  void executeCallback();

 private:
  bool _hasWaited;
  WaitingBehaviour _variant;
  bool _doesContainShadowRows{false};
  bool _shouldLieOnLastRow{false};
  darbotdb::aql::RegisterInfos _infos;
  typename darbotdb::aql::ScatterExecutor::ClientBlockData _blockData;
  std::function<void()> _wakeUpCallback;
  std::function<void()> _executeCallback;
  darbotdb::aql::AqlCall _lastCall;
};
}  // namespace aql

}  // namespace tests
}  // namespace darbotdb
