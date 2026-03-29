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

#include "IResearchLinkMock.h"

#include "Mocks/StorageEngineMock.h"
#include "Basics/DownCast.h"
#include "Cluster/ServerState.h"
#include "IResearch/IResearchCommon.h"
#include "IResearch/IResearchLinkHelper.h"
#include "IResearch/IResearchView.h"
#include "Indexes/IndexFactory.h"
#include "Logger/LogMacros.h"
#include "Logger/Logger.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "StorageEngine/StorageEngine.h"
#include "Transaction/Methods.h"
#include "VocBase/LogicalCollection.h"

namespace darbotdb::iresearch {

IResearchLinkMock::IResearchLinkMock(IndexId iid, LogicalCollection& collection)
    : Index{iid, collection, IResearchLinkHelper::emptyIndexSlice(0).slice()},
      IResearchLink{collection.vocbase().server(), collection} {
  TRI_ASSERT(!ServerState::instance()->isCoordinator());
  _unique = false;  // cannot be unique since multiple fields are indexed
  _sparse = true;   // always sparse
}

void IResearchLinkMock::toVelocyPack(
    darbotdb::velocypack::Builder& builder,
    std::underlying_type<darbotdb::Index::Serialize>::type flags) const {
  if (builder.isOpenObject()) {
    THROW_DDB_EXCEPTION(darbotdb::Result(  // result
        TRI_ERROR_BAD_PARAMETER,              // code
        std::string("failed to generate link definition for arangosearch view "
                    "link '") +
            std::to_string(darbotdb::Index::id().id()) + "'"));
  }

  auto forPersistence =  // definition for persistence
      darbotdb::Index::hasFlag(flags, darbotdb::Index::Serialize::Internals);

  builder.openObject();

  if (!properties(builder, forPersistence).ok()) {
    THROW_DDB_EXCEPTION(darbotdb::Result(  // result
        TRI_ERROR_INTERNAL,                   // code
        std::string("failed to generate link definition for arangosearch view "
                    "link '") +
            std::to_string(darbotdb::Index::id().id()) + "'"));
  }

  if (darbotdb::Index::hasFlag(flags, darbotdb::Index::Serialize::Figures)) {
    builder.add("figures", VPackValue(VPackValueType::Object));
    toVelocyPackFigures(builder);
    builder.close();
  }

  builder.close();
}

std::function<irs::directory_attributes()> IResearchLinkMock::InitCallback;

Result IResearchLinkMock::remove(transaction::Methods& trx,
                                 LocalDocumentId documentId) {
  auto* state = basics::downCast<::TransactionStateMock>(trx.state());
  TRI_ASSERT(state != nullptr);
  state->incrementRemove();
  return IResearchDataStore::remove(trx, documentId);
}

}  // namespace darbotdb::iresearch
