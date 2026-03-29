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
/// @author Tobias Gödderz
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Aql/AqlItemBlockManager.h"
#include "Aql/DependencyProxy.h"
#include "Aql/ExecutionState.h"
#include "Aql/SharedAqlItemBlockPtr.h"
#include "Aql/types.h"
#include "Basics/ResourceUsage.h"

#include <cstdint>
#include <queue>

namespace darbotdb {
namespace aql {
class SkipResult;
}
namespace tests {
namespace aql {

template<::darbotdb::aql::BlockPassthrough passBlocksThrough>
class DependencyProxyMock
    : public ::darbotdb::aql::DependencyProxy<passBlocksThrough> {
 public:
  explicit DependencyProxyMock(darbotdb::ResourceMonitor& monitor,
                               ::darbotdb::aql::RegisterCount nrRegisters);

  // mock methods
  size_t numberDependencies() const noexcept override { return 1; }

  std::tuple<darbotdb::aql::ExecutionState, darbotdb::aql::SkipResult,
             darbotdb::aql::SharedAqlItemBlockPtr>
  execute(darbotdb::aql::AqlCallStack const& stack) override;

 private:
  using FetchBlockReturnItem = std::pair<darbotdb::aql::ExecutionState,
                                         darbotdb::aql::SharedAqlItemBlockPtr>;

 public:
  // additional test methods
  DependencyProxyMock& shouldReturn(
      darbotdb::aql::ExecutionState,
      darbotdb::aql::SharedAqlItemBlockPtr const&);
  DependencyProxyMock& shouldReturn(FetchBlockReturnItem);
  DependencyProxyMock& shouldReturn(std::vector<FetchBlockReturnItem>);
  DependencyProxyMock& andThenReturn(FetchBlockReturnItem);
  DependencyProxyMock& andThenReturn(
      darbotdb::aql::ExecutionState,
      darbotdb::aql::SharedAqlItemBlockPtr const&);
  DependencyProxyMock& andThenReturn(std::vector<FetchBlockReturnItem>);

  bool allBlocksFetched() const;
  size_t numFetchBlockCalls() const;

 private:
  std::queue<FetchBlockReturnItem> _itemsToReturn;

  size_t _numFetchBlockCalls;

  ::darbotdb::ResourceMonitor& _monitor;
  ::darbotdb::aql::AqlItemBlockManager _itemBlockManager;
  ::darbotdb::aql::SharedAqlItemBlockPtr _block;
};

template<::darbotdb::aql::BlockPassthrough passBlocksThrough>
class MultiDependencyProxyMock
    : public ::darbotdb::aql::DependencyProxy<passBlocksThrough> {
 public:
  MultiDependencyProxyMock(darbotdb::ResourceMonitor& monitor,
                           ::darbotdb::aql::RegIdSet const& inputRegisters,
                           ::darbotdb::aql::RegisterCount nrRegisters,
                           size_t nrDeps);

  // mock methods

  size_t numberDependencies() const noexcept override {
    return _dependencyMocks.size();
  }

  // additional test methods
  DependencyProxyMock<passBlocksThrough>& getDependencyMock(size_t dependency) {
    TRI_ASSERT(dependency < _dependencyMocks.size());
    return *_dependencyMocks[dependency];
  }
  bool allBlocksFetched() const;

  size_t numFetchBlockCalls() const;

 private:
  std::vector<std::unique_ptr<DependencyProxyMock<passBlocksThrough>>>
      _dependencyMocks;
  ::darbotdb::aql::AqlItemBlockManager _itemBlockManager;
};

}  // namespace aql
}  // namespace tests
}  // namespace darbotdb
