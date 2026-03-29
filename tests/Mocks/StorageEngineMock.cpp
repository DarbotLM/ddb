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

#include "StorageEngineMock.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "Aql/AstNode.h"
#include "Basics/Result.h"
#include "Basics/DownCast.h"
#include "Basics/StaticStrings.h"
#include "Basics/VelocyPackHelper.h"
#include "Basics/asio_ns.h"
#include "Cluster/ClusterFeature.h"
#include "Cluster/ClusterInfo.h"
#include "ClusterEngine/ClusterEngine.h"
#include "ClusterEngine/ClusterIndexFactory.h"
#include "IResearch/IResearchCommon.h"
#include "IResearch/IResearchFeature.h"
#include "IResearch/IResearchInvertedIndex.h"
#include "IResearch/IResearchLinkCoordinator.h"
#include "IResearch/IResearchRocksDBLink.h"
#include "IResearch/VelocyPackHelper.h"
#include "Indexes/IndexIterator.h"
#include "Indexes/SimpleAttributeEqualityMatcher.h"
#include "Indexes/SortedIndexAttributeMatcher.h"
#include "Replication2/ReplicatedLog/LogCommon.h"
#include "RestServer/FlushFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "Transaction/Helpers.h"
#include "Transaction/Hints.h"
#include "Transaction/Manager.h"
#include "Transaction/ManagerFeature.h"
#include "Transaction/Methods.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/OperationOptions.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/LogicalView.h"
#include "VocBase/ticks.h"
#include "Futures/Future.h"

#include "Mocks/IResearchLinkMock.h"
#include "Mocks/IResearchInvertedIndexMock.h"
#include "Mocks/PhysicalCollectionMock.h"

#include <velocypack/Collection.h>
#include <velocypack/Iterator.h>

namespace {

struct IndexFactoryMock : darbotdb::IndexFactory {
  IndexFactoryMock(darbotdb::ArangodServer& server, bool injectClusterIndexes)
      : IndexFactory(server) {
    if (injectClusterIndexes) {
      darbotdb::ClusterIndexFactory::linkIndexFactories(
          server, *this,
          server.getFeature<darbotdb::EngineSelectorFeature>()
              .engine<darbotdb::ClusterEngine>());
    }
  }

  virtual void fillSystemIndexes(darbotdb::LogicalCollection& col,
                                 std::vector<std::shared_ptr<darbotdb::Index>>&
                                     systemIndexes) const override {
    // NOOP
  }

  /// @brief create indexes from a list of index definitions
  virtual void prepareIndexes(
      darbotdb::LogicalCollection& col,
      darbotdb::velocypack::Slice indexesSlice,
      std::vector<std::shared_ptr<darbotdb::Index>>& indexes) const override {
    // NOOP
  }
};

}  // namespace

std::shared_ptr<darbotdb::iresearch::IResearchLinkMock>
StorageEngineMock::buildLinkMock(darbotdb::IndexId id,
                                 darbotdb::LogicalCollection& collection,
                                 VPackSlice const& info) {
  auto index = std::shared_ptr<darbotdb::iresearch::IResearchLinkMock>(
      new darbotdb::iresearch::IResearchLinkMock(id, collection));
  bool pathExists = false;
  auto cleanup = darbotdb::scopeGuard([&]() noexcept {
    if (pathExists) {
      try {
        index->unload();  // TODO(MBkkt) unload should be implicit noexcept?
      } catch (...) {
      }
    } else {
      index->drop();
    }
  });
  auto res =
      static_cast<darbotdb::iresearch::IResearchLinkMock*>(index.get())
          ->init(info, pathExists, []() -> irs::directory_attributes {
            if (darbotdb::iresearch::IResearchLinkMock::InitCallback !=
                nullptr) {
              return darbotdb::iresearch::IResearchLinkMock::InitCallback();
            }
            return irs::directory_attributes{};
          });

  if (!res.ok()) {
    THROW_DDB_EXCEPTION(res);
  }
  cleanup.cancel();

  return index;
}

std::shared_ptr<darbotdb::iresearch::IResearchInvertedIndexMock>
StorageEngineMock::buildInvertedIndexMock(
    darbotdb::IndexId id, darbotdb::LogicalCollection& collection,
    VPackSlice const& info) {
  std::string name;

  if (info.isObject()) {
    VPackSlice sub = info.get(darbotdb::StaticStrings::IndexName);
    if (sub.isString()) {
      name = sub.copyString();
    }
  }
  if (name.empty()) {
    // we couldn't extract name of index for some reason
    THROW_DDB_EXCEPTION(TRI_ERROR_DDB_ATTRIBUTE_PARSER_FAILED);
  }

  darbotdb::iresearch::IResearchInvertedIndexMeta meta;
  std::string errField;
  meta.init(collection.vocbase().server(), info, false, errField,
            collection.vocbase().name());

  auto attrs = darbotdb::iresearch::IResearchInvertedIndex::fields(meta);
  auto index = std::shared_ptr<darbotdb::iresearch::IResearchInvertedIndexMock>(
      new darbotdb::iresearch::IResearchInvertedIndexMock(id, collection, name,
                                                          attrs, false, true));

  bool pathExists = false;
  auto cleanup = darbotdb::scopeGuard([&]() noexcept {
    if (pathExists) {
      try {
        index->unload();  // TODO(MBkkt) unload should be implicit noexcept?
      } catch (...) {
      }
    } else {
      index->drop();
    }
  });

  auto res =
      static_cast<darbotdb::iresearch::IResearchInvertedIndexMock*>(index.get())
          ->init(info, pathExists, []() -> irs::directory_attributes {
            if (darbotdb::iresearch::IResearchInvertedIndexMock::InitCallback !=
                nullptr) {
              return darbotdb::iresearch::IResearchInvertedIndexMock::
                  InitCallback();
            }
            return irs::directory_attributes{};
          });

  if (!res.ok()) {
    THROW_DDB_EXCEPTION(res);
  }
  cleanup.cancel();
  return index;
}

std::function<void()> StorageEngineMock::before = []() -> void {};
darbotdb::RecoveryState StorageEngineMock::recoveryStateResult =
    darbotdb::RecoveryState::DONE;
TRI_voc_tick_t StorageEngineMock::recoveryTickResult = 0;
std::function<void()> StorageEngineMock::recoveryTickCallback = []() -> void {};

/*static*/ std::string StorageEngineMock::versionFilenameResult;

StorageEngineMock::StorageEngineMock(darbotdb::ArangodServer& server,
                                     bool injectClusterIndexes)
    : StorageEngine(server, "Mock", "", std::numeric_limits<size_t>::max(),
                    std::unique_ptr<darbotdb::IndexFactory>(
                        new IndexFactoryMock(server, injectClusterIndexes))),
      vocbaseCount(1),
      _releasedTick(0) {}

darbotdb::HealthData StorageEngineMock::healthCheck() { return {}; }

darbotdb::WalAccess const* StorageEngineMock::walAccess() const {
  TRI_ASSERT(false);
  return nullptr;
}

bool StorageEngineMock::autoRefillIndexCaches() const { return false; }

bool StorageEngineMock::autoRefillIndexCachesOnFollowers() const {
  return false;
}

void StorageEngineMock::addOptimizerRules(
    darbotdb::aql::OptimizerRulesFeature& /*feature*/) {
  before();
  // NOOP
}

void StorageEngineMock::addRestHandlers(
    darbotdb::rest::RestHandlerFactory& handlerFactory) {
  TRI_ASSERT(false);
}

#ifdef USE_V8
void StorageEngineMock::addV8Functions() { TRI_ASSERT(false); }
#endif

void StorageEngineMock::changeCollection(
    TRI_vocbase_t& vocbase, darbotdb::LogicalCollection const& collection) {
  // NOOP, assume physical collection changed OK
}

darbotdb::Result StorageEngineMock::changeView(
    darbotdb::LogicalView const& view, darbotdb::velocypack::Slice update) {
  before();
  std::pair key{view.vocbase().id(), view.id()};
  TRI_ASSERT(views.find(key) != views.end());
  views.emplace(key, update);
  return {};
}

void StorageEngineMock::createCollection(
    TRI_vocbase_t& vocbase, darbotdb::LogicalCollection const& collection) {}

darbotdb::Result StorageEngineMock::createLoggerState(TRI_vocbase_t*,
                                                      VPackBuilder&) {
  TRI_ASSERT(false);
  return darbotdb::Result(TRI_ERROR_NOT_IMPLEMENTED);
}

std::unique_ptr<darbotdb::PhysicalCollection>
StorageEngineMock::createPhysicalCollection(
    darbotdb::LogicalCollection& collection,
    darbotdb::velocypack::Slice /*info*/) {
  before();
  return std::make_unique<PhysicalCollectionMock>(collection);
}

darbotdb::Result StorageEngineMock::createTickRanges(VPackBuilder&) {
  TRI_ASSERT(false);
  return darbotdb::Result(TRI_ERROR_NOT_IMPLEMENTED);
}

std::unique_ptr<darbotdb::transaction::Manager>
StorageEngineMock::createTransactionManager(
    darbotdb::transaction::ManagerFeature& feature) {
  return std::make_unique<darbotdb::transaction::Manager>(feature);
}

std::shared_ptr<darbotdb::TransactionState>
StorageEngineMock::createTransactionState(
    TRI_vocbase_t& vocbase, darbotdb::TransactionId tid,
    darbotdb::transaction::Options const& options,
    darbotdb::transaction::OperationOrigin operationOrigin) {
  return std::make_shared<TransactionStateMock>(vocbase, tid, options,
                                                operationOrigin, *this);
}

darbotdb::Result StorageEngineMock::createView(
    TRI_vocbase_t& vocbase, darbotdb::DataSourceId id,
    darbotdb::LogicalView const& view) {
  before();
  TRI_ASSERT(views.find(std::make_pair(vocbase.id(), view.id())) ==
             views.end());  // called after createView()
  darbotdb::velocypack::Builder builder;

  builder.openObject();
  auto res = view.properties(
      builder, darbotdb::LogicalDataSource::Serialization::Persistence);
  if (!res.ok()) {
    return res;
  }
  builder.close();
  views[std::make_pair(vocbase.id(), view.id())] = std::move(builder);

  return darbotdb::Result(TRI_ERROR_NO_ERROR);  // assume mock view persisted OK
}

darbotdb::Result StorageEngineMock::compactAll(bool changeLevels,
                                               bool compactBottomMostLevel) {
  TRI_ASSERT(false);
  return darbotdb::Result();
}

TRI_voc_tick_t StorageEngineMock::currentTick() const {
  return _engineTick.load();
}

darbotdb::Result StorageEngineMock::dropCollection(
    TRI_vocbase_t& vocbase, darbotdb::LogicalCollection& collection) {
  return darbotdb::Result(
      TRI_ERROR_NO_ERROR);  // assume physical collection dropped OK
}

darbotdb::Result StorageEngineMock::dropDatabase(TRI_vocbase_t& vocbase) {
  TRI_ASSERT(false);
  return darbotdb::Result();
}

darbotdb::Result StorageEngineMock::dropView(
    TRI_vocbase_t const& vocbase, darbotdb::LogicalView const& view) {
  before();
  TRI_ASSERT(views.find(std::make_pair(vocbase.id(), view.id())) !=
             views.end());
  views.erase(std::make_pair(vocbase.id(), view.id()));

  return darbotdb::Result(TRI_ERROR_NO_ERROR);  // assume mock view dropped OK
}

darbotdb::Result StorageEngineMock::firstTick(uint64_t&) {
  TRI_ASSERT(false);
  return darbotdb::Result(TRI_ERROR_NOT_IMPLEMENTED);
}

void StorageEngineMock::getCollectionInfo(TRI_vocbase_t& vocbase,
                                          darbotdb::DataSourceId cid,
                                          darbotdb::velocypack::Builder& result,
                                          bool includeIndexes,
                                          TRI_voc_tick_t maxTick) {
  darbotdb::velocypack::Builder parameters;

  parameters.openObject();
  parameters.close();

  result.openObject();
  result.add("parameters",
             parameters.slice());  // required entry of type object
  result.close();

  // nothing more required, assume info used for PhysicalCollectionMock
}

ErrorCode StorageEngineMock::getCollectionsAndIndexes(
    TRI_vocbase_t& vocbase, darbotdb::velocypack::Builder& result,
    bool wasCleanShutdown, bool isUpgrade) {
  TRI_ASSERT(false);
  return TRI_ERROR_INTERNAL;
}

void StorageEngineMock::getDatabases(darbotdb::velocypack::Builder& result) {
  before();
  darbotdb::velocypack::Builder system;

  system.openObject();
  system.add("name", darbotdb::velocypack::Value(
                         darbotdb::StaticStrings::SystemDatabase));
  system.close();

  // array expected
  result.openArray();
  result.add(system.slice());
  result.close();
}

void StorageEngineMock::cleanupReplicationContexts() {
  // nothing to do here
}

darbotdb::velocypack::Builder
StorageEngineMock::getReplicationApplierConfiguration(TRI_vocbase_t& vocbase,
                                                      ErrorCode& result) {
  before();
  result =
      TRI_ERROR_FILE_NOT_FOUND;  // assume no ReplicationApplierConfiguration
                                 // for vocbase

  return darbotdb::velocypack::Builder();
}

darbotdb::velocypack::Builder
StorageEngineMock::getReplicationApplierConfiguration(ErrorCode& status) {
  before();
  status = TRI_ERROR_FILE_NOT_FOUND;

  return darbotdb::velocypack::Builder();
}

ErrorCode StorageEngineMock::getViews(TRI_vocbase_t& vocbase,
                                      darbotdb::velocypack::Builder& result) {
  result.openArray();

  for (auto& entry : views) {
    result.add(entry.second.slice());
  }

  result.close();

  return TRI_ERROR_NO_ERROR;
}

darbotdb::Result StorageEngineMock::handleSyncKeys(
    darbotdb::DatabaseInitialSyncer& syncer, darbotdb::LogicalCollection& col,
    std::string const& keysId) {
  TRI_ASSERT(false);
  return darbotdb::Result();
}

darbotdb::RecoveryState StorageEngineMock::recoveryState() {
  return recoveryStateResult;
}
TRI_voc_tick_t StorageEngineMock::recoveryTick() {
  if (recoveryTickCallback) {
    recoveryTickCallback();
  }
  return recoveryTickResult;
}

darbotdb::Result StorageEngineMock::lastLogger(
    TRI_vocbase_t& vocbase, uint64_t tickStart, uint64_t tickEnd,
    darbotdb::velocypack::Builder& builderSPtr) {
  TRI_ASSERT(false);
  return darbotdb::Result(TRI_ERROR_NOT_IMPLEMENTED);
}

std::unique_ptr<TRI_vocbase_t> StorageEngineMock::openDatabase(
    darbotdb::CreateDatabaseInfo&& info, bool isUpgrade) {
  before();

  auto new_info = info;
  new_info.setId(++vocbaseCount);

  return std::make_unique<TRI_vocbase_t>(std::move(new_info), _versionTracker,
                                         true);
}

TRI_voc_tick_t StorageEngineMock::releasedTick() const {
  before();
  return _releasedTick;
}

void StorageEngineMock::releaseTick(TRI_voc_tick_t tick) {
  before();
  _releasedTick = tick;
}

ErrorCode StorageEngineMock::removeReplicationApplierConfiguration(
    TRI_vocbase_t& vocbase) {
  TRI_ASSERT(false);
  return TRI_ERROR_NO_ERROR;
}

ErrorCode StorageEngineMock::removeReplicationApplierConfiguration() {
  TRI_ASSERT(false);
  return TRI_ERROR_NO_ERROR;
}

darbotdb::Result StorageEngineMock::renameCollection(
    TRI_vocbase_t& vocbase, darbotdb::LogicalCollection const& collection,
    std::string const& oldName) {
  TRI_ASSERT(false);
  return darbotdb::Result(TRI_ERROR_INTERNAL);
}

ErrorCode StorageEngineMock::saveReplicationApplierConfiguration(
    TRI_vocbase_t& vocbase, darbotdb::velocypack::Slice slice, bool doSync) {
  TRI_ASSERT(false);
  return TRI_ERROR_NO_ERROR;
}

ErrorCode StorageEngineMock::saveReplicationApplierConfiguration(
    darbotdb::velocypack::Slice, bool) {
  TRI_ASSERT(false);
  return TRI_ERROR_NO_ERROR;
}

std::string StorageEngineMock::versionFilename(TRI_voc_tick_t) const {
  return versionFilenameResult;
}

void StorageEngineMock::waitForEstimatorSync() { TRI_ASSERT(false); }

std::vector<std::string> StorageEngineMock::currentWalFiles() const {
  return std::vector<std::string>();
}

darbotdb::Result StorageEngineMock::flushWal(bool waitForSync,
                                             bool waitForCollector) {
  TRI_ASSERT(false);
  return darbotdb::Result();
}
darbotdb::Result StorageEngineMock::dropReplicatedState(
    TRI_vocbase_t& vocbase,
    std::unique_ptr<darbotdb::replication2::storage::IStorageEngineMethods>&
        ptr) {
  TRI_ASSERT(false);
  THROW_DDB_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}
darbotdb::ResultT<
    std::unique_ptr<darbotdb::replication2::storage::IStorageEngineMethods>>
StorageEngineMock::createReplicatedState(
    TRI_vocbase_t& vocbase, darbotdb::replication2::LogId id,
    const darbotdb::replication2::storage::PersistedStateInfo& info) {
  TRI_ASSERT(false);
  THROW_DDB_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

class TransactionCollectionMock : public darbotdb::TransactionCollection {
 public:
  TransactionCollectionMock(darbotdb::TransactionState* state,
                            darbotdb::DataSourceId cid,
                            darbotdb::AccessMode::Type accessType);
  bool canAccess(darbotdb::AccessMode::Type accessType) const override;
  bool hasOperations() const override;
  void releaseUsage() override;
  darbotdb::futures::Future<darbotdb::Result> lockUsage() override;

 private:
  darbotdb::futures::Future<darbotdb::Result> doLock(
      darbotdb::AccessMode::Type type) override;
  darbotdb::Result doUnlock(darbotdb::AccessMode::Type type) override;
};

TransactionCollectionMock::TransactionCollectionMock(
    darbotdb::TransactionState* state, darbotdb::DataSourceId cid,
    darbotdb::AccessMode::Type accessType)
    : TransactionCollection(state, cid, accessType) {}

bool TransactionCollectionMock::canAccess(
    darbotdb::AccessMode::Type accessType) const {
  return nullptr != _collection;  // collection must have be opened previously
}

bool TransactionCollectionMock::hasOperations() const {
  TRI_ASSERT(false);
  return false;
}

void TransactionCollectionMock::releaseUsage() {
  if (_collection) {
    if (!darbotdb::ServerState::instance()->isCoordinator()) {
      _transaction->vocbase().releaseCollection(_collection.get());
    }
    _collection = nullptr;
  }
}

darbotdb::futures::Future<darbotdb::Result>
TransactionCollectionMock::lockUsage() {
  bool shouldLock = !darbotdb::AccessMode::isNone(_accessType);

  if (shouldLock && !isLocked()) {
    // r/w lock the collection
    darbotdb::Result res = co_await doLock(_accessType);

    if (res.is(TRI_ERROR_LOCKED)) {
      // TRI_ERROR_LOCKED is not an error, but it indicates that the lock
      // operation has actually acquired the lock (and that the lock has not
      // been held before)
      res.reset();
    } else if (res.fail()) {
      co_return res;
    }
  }

  if (!_collection) {
    if (darbotdb::ServerState::instance()->isCoordinator()) {
      auto& ci = _transaction->vocbase()
                     .server()
                     .getFeature<darbotdb::ClusterFeature>()
                     .clusterInfo();
      _collection = ci.getCollectionNT(_transaction->vocbase().name(),
                                       std::to_string(_cid.id()));
    } else {
      _collection = _transaction->vocbase().useCollection(_cid, true);
    }
  }

  co_return darbotdb::Result(_collection
                                 ? TRI_ERROR_NO_ERROR
                                 : TRI_ERROR_DDB_DATA_SOURCE_NOT_FOUND);
}

darbotdb::futures::Future<darbotdb::Result> TransactionCollectionMock::doLock(
    darbotdb::AccessMode::Type type) {
  if (_lockType > _accessType) {
    co_return {TRI_ERROR_INTERNAL};
  }

  _lockType = type;

  co_return {};
}

darbotdb::Result TransactionCollectionMock::doUnlock(
    darbotdb::AccessMode::Type type) {
  if (_lockType != type) {
    return {TRI_ERROR_INTERNAL};
  }

  _lockType = darbotdb::AccessMode::Type::NONE;

  return {};
}

std::atomic_size_t TransactionStateMock::abortTransactionCount{0};
std::atomic_size_t TransactionStateMock::beginTransactionCount{0};
std::atomic_size_t TransactionStateMock::commitTransactionCount{0};

// ensure each transaction state has a unique ID
TransactionStateMock::TransactionStateMock(
    TRI_vocbase_t& vocbase, darbotdb::TransactionId tid,
    darbotdb::transaction::Options const& options,
    darbotdb::transaction::OperationOrigin operationOrigin,
    StorageEngineMock& engine)
    : TransactionState(vocbase, tid, options, operationOrigin),
      _engine{engine} {}

darbotdb::Result TransactionStateMock::abortTransaction(
    darbotdb::transaction::Methods* trx) {
  ++abortTransactionCount;
  updateStatus(darbotdb::transaction::Status::ABORTED);
  //  releaseUsage();
  resetTransactionId();

  return darbotdb::Result();
}

darbotdb::futures::Future<darbotdb::Result>
TransactionStateMock::beginTransaction(darbotdb::transaction::Hints hints) {
  ++beginTransactionCount;
  _hints = hints;

  darbotdb::Result res = co_await useCollections();
  if (res.fail()) {  // something is wrong
    co_return res;
  }

  if (!res.ok()) {
    updateStatus(darbotdb::transaction::Status::ABORTED);
    resetTransactionId();
    co_return res;
  }
  updateStatus(darbotdb::transaction::Status::RUNNING);
  co_return darbotdb::Result();
}

darbotdb::futures::Future<darbotdb::Result>
TransactionStateMock::commitTransaction(darbotdb::transaction::Methods* trx) {
  applyBeforeCommitCallbacks();
  TRI_ASSERT(this == trx->state());
  _engine.incrementTick(numPrimitiveOperations() + 1);
  ++commitTransactionCount;
  updateStatus(darbotdb::transaction::Status::COMMITTED);
  resetTransactionId();
  //  releaseUsage();
  applyAfterCommitCallbacks();
  return darbotdb::Result();
}

darbotdb::Result TransactionStateMock::triggerIntermediateCommit() {
  ADB_PROD_ASSERT(false) << "triggerIntermediateCommit is not supported in "
                            "TransactionStateMock";
  return darbotdb::Result{TRI_ERROR_INTERNAL};
}

darbotdb::futures::Future<darbotdb::Result>
TransactionStateMock::performIntermediateCommitIfRequired(
    darbotdb::DataSourceId cid) {
  return darbotdb::Result();
}

uint64_t TransactionStateMock::numCommits() const noexcept {
  return commitTransactionCount;
}

uint64_t TransactionStateMock::numIntermediateCommits() const noexcept {
  return 0;
}

void TransactionStateMock::addIntermediateCommits(uint64_t /*value*/) {
  // should never be called during testing
  TRI_ASSERT(false);
}

bool TransactionStateMock::hasFailedOperations() const noexcept {
  return false;  // assume no failed operations
}

TRI_voc_tick_t TransactionStateMock::lastOperationTick() const noexcept {
  return _engine.currentTick();
}

std::unique_ptr<darbotdb::TransactionCollection>
TransactionStateMock::createTransactionCollection(
    darbotdb::DataSourceId cid, darbotdb::AccessMode::Type accessType) {
  return std::make_unique<TransactionCollectionMock>(this, cid, accessType);
}
