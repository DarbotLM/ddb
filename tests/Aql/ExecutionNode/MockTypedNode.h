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

#include "Aql/ExecutionNode/ExecutionNode.h"
#include "Aql/ExecutionNodeId.h"

namespace darbotdb::tests::aql {

// An execution node pretending to be of an arbitrary type.
class MockTypedNode : public ::darbotdb::aql::ExecutionNode {
  friend class ::darbotdb::aql::ExecutionBlock;

 public:
  MockTypedNode(::darbotdb::aql::ExecutionPlan* plan,
                darbotdb::aql::ExecutionNodeId id, NodeType);

  // return mocked type
  NodeType getType() const override final;

  /// @brief return the amount of bytes used
  size_t getMemoryUsedBytes() const override final;

  // Necessary overrides, all not implemented:

  void doToVelocyPack(darbotdb::velocypack::Builder&,
                      unsigned flags) const override;

  std::unique_ptr<::darbotdb::aql::ExecutionBlock> createBlock(
      ::darbotdb::aql::ExecutionEngine& engine) const override;

  ExecutionNode* clone(::darbotdb::aql::ExecutionPlan* plan,
                       bool withDependencies) const override;

  ::darbotdb::aql::CostEstimate estimateCost() const override;

 private:
  NodeType _mockedType{};
};

}  // namespace darbotdb::tests::aql
