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
/// @author Tobias Goedderz
/// @author Michael Hackstein
/// @author Heiko Kernbach
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Aql/AqlItemBlockManager.h"
#include "Aql/ConstFetcher.h"
#include "Aql/ExecutionBlock.h"
#include "Aql/ExecutionState.h"
#include "Aql/InputAqlItemRow.h"
#include "Aql/SingleRowFetcher.h"
#include "Aql/VelocyPackHelper.h"
#include "Basics/GlobalResourceMonitor.h"
#include "Basics/ResourceUsage.h"

#include <velocypack/Buffer.h>
#include <velocypack/Builder.h>
#include <velocypack/Slice.h>

namespace darbotdb {

namespace aql {
class AqlItemBlock;
class AqlItemBlockManager;
class InputAqlItemRow;
}  // namespace aql

namespace tests {
namespace aql {

/**
 * @brief Mock for SingleRowFetcher
 */
template<::darbotdb::aql::BlockPassthrough passBlocksThrough>
class SingleRowFetcherHelper
    : public darbotdb::aql::SingleRowFetcher<passBlocksThrough> {
 public:
  SingleRowFetcherHelper(darbotdb::aql::AqlItemBlockManager& manager,
                         size_t blockSize, bool returnsWaiting,
                         darbotdb::aql::SharedAqlItemBlockPtr input);

  // backwards compatible constructor
  SingleRowFetcherHelper(
      darbotdb::aql::AqlItemBlockManager& manager,
      std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> const& vPackBuffer,
      bool returnsWaiting);

  virtual ~SingleRowFetcherHelper();

  uint64_t nrCalled() { return _nrCalled; }
  uint64_t nrReturned() { return _nrReturned; }
  uint64_t nrItems() { return _nrItems; }

  size_t totalSkipped() const { return _totalSkipped; }

  darbotdb::aql::AqlItemBlockManager& itemBlockManager() {
    return _itemBlockManager;
  }

  bool isDone() const { return _returnedDoneOnFetchRow; };

 private:
  darbotdb::aql::SharedAqlItemBlockPtr& getItemBlock() { return _itemBlock; }
  darbotdb::aql::SharedAqlItemBlockPtr const& getItemBlock() const {
    return _itemBlock;
  }

  void nextRow() {
    _curRowIndex++;
    _curIndexInBlock = (_curIndexInBlock + 1) % _blockSize;
  }

  bool wait() {
    // Wait on the first row of every block, if applicable
    if (_returnsWaiting && _curIndexInBlock == 0) {
      // If the insert succeeds, we have not yet waited at this index.
      bool const waitNow = _didWaitAt.insert(_curRowIndex).second;

      return waitNow;
    }

    return false;
  }

 private:
  bool _returnedDoneOnFetchRow = false;
  bool _returnedDoneOnFetchShadowRow = false;
  bool const _returnsWaiting;
  uint64_t _nrItems;
  uint64_t _nrCalled{};
  uint64_t _nrReturned{};
  size_t _skipped{};
  size_t _totalSkipped{};
  size_t _curIndexInBlock{};
  size_t _curRowIndex{};
  size_t _blockSize;
  std::unordered_set<size_t> _didWaitAt;
  darbotdb::aql::AqlItemBlockManager& _itemBlockManager;
  darbotdb::aql::SharedAqlItemBlockPtr _itemBlock;
  darbotdb::aql::InputAqlItemRow _lastReturnedRow{
      darbotdb::aql::CreateInvalidInputRowHint{}};
};

class ConstFetcherHelper : public darbotdb::aql::ConstFetcher {
 public:
  ConstFetcherHelper(
      darbotdb::aql::AqlItemBlockManager& itemBlockManager,
      std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> vPackBuffer);
  virtual ~ConstFetcherHelper();

 private:
  std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> _vPackBuffer;
  darbotdb::velocypack::Slice _data;
};

}  // namespace aql
}  // namespace tests
}  // namespace darbotdb
