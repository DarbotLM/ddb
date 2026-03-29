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

#include "Basics/Result.h"
#include "Futures/Future.h"
#include "StorageEngine/HealthData.h"
#include "StorageEngine/StorageEngine.h"
#include "StorageEngine/TransactionState.h"
#include "VocBase/Identifiers/IndexId.h"

#include <atomic>
#include <memory>
#include <string_view>

namespace darbotdb {

class PhysicalCollection;
class TransactionCollection;
class TransactionManager;
class WalAccess;
namespace aql {
class OptimizerRulesFeature;
}
namespace iresearch {
class IResearchLinkMock;
class IResearchInvertedIndexMock;
}  // namespace iresearch

}  // namespace darbotdb

class StorageEngineMock;

class TransactionStateMock : public darbotdb::TransactionState {
 public:
  static std::atomic_size_t abortTransactionCount;
  static std::atomic_size_t beginTransactionCount;
  static std::atomic_size_t commitTransactionCount;

  TransactionStateMock(TRI_vocbase_t& vocbase, darbotdb::TransactionId tid,
                       darbotdb::transaction::Options const& options,
                       darbotdb::transaction::OperationOrigin operationOrigin,
                       StorageEngineMock& engine);
  [[nodiscard]] bool ensureSnapshot() override { return false; }
  darbotdb::Result abortTransaction(
      darbotdb::transaction::Methods* trx) override;
  darbotdb::futures::Future<darbotdb::Result> beginTransaction(
      darbotdb::transaction::Hints hints) override;
  darbotdb::futures::Future<darbotdb::Result> commitTransaction(
      darbotdb::transaction::Methods* trx) override;
  darbotdb::futures::Future<darbotdb::Result>
  performIntermediateCommitIfRequired(darbotdb::DataSourceId cid) override;

  void incrementInsert() noexcept { ++_numInserts; }
  void incrementRemove() noexcept { ++_numRemoves; }
  uint64_t numPrimitiveOperations() const noexcept override {
    return _numInserts + _numRemoves;
  }

  uint64_t numCommits() const noexcept override;
  uint64_t numIntermediateCommits() const noexcept override;
  void addIntermediateCommits(uint64_t value) override;
  bool hasFailedOperations() const noexcept override;
  TRI_voc_tick_t lastOperationTick() const noexcept override;

  darbotdb::Result triggerIntermediateCommit() override;
  std::unique_ptr<darbotdb::TransactionCollection> createTransactionCollection(
      darbotdb::DataSourceId cid,
      darbotdb::AccessMode::Type accessType) override;

 private:
  uint64_t _numInserts{0};
  uint64_t _numRemoves{0};
  StorageEngineMock& _engine;
};

class StorageEngineMockSnapshot final : public darbotdb::StorageSnapshot {
 public:
  StorageEngineMockSnapshot(TRI_voc_tick_t t) : _t(t) {}
  TRI_voc_tick_t tick() const noexcept override { return _t; }

 private:
  TRI_voc_tick_t _t;
};

class StorageEngineMock : public darbotdb::StorageEngine {
 public:
  static std::function<void()> before;
  static darbotdb::Result flushSubscriptionResult;
  static darbotdb::RecoveryState recoveryStateResult;
  static TRI_voc_tick_t recoveryTickResult;
  static std::string versionFilenameResult;
  static std::function<void()> recoveryTickCallback;
  std::map<std::pair<TRI_voc_tick_t, darbotdb::DataSourceId>,
           darbotdb::velocypack::Builder>
      views;
  std::atomic<size_t> vocbaseCount;

  explicit StorageEngineMock(darbotdb::ArangodServer& server,
                             bool injectClusterIndexes = false);
  darbotdb::HealthData healthCheck() override;
  void addOptimizerRules(
      darbotdb::aql::OptimizerRulesFeature& feature) override;
  void addRestHandlers(
      darbotdb::rest::RestHandlerFactory& handlerFactory) override;
#ifdef USE_V8
  void addV8Functions() override;
#endif
  void changeCollection(TRI_vocbase_t& vocbase,
                        darbotdb::LogicalCollection const& collection) override;
  darbotdb::Result changeView(darbotdb::LogicalView const& view,
                              darbotdb::velocypack::Slice update) override;

  void createCollection(TRI_vocbase_t& vocbase,
                        darbotdb::LogicalCollection const& collection) override;
  darbotdb::Result createLoggerState(TRI_vocbase_t*, VPackBuilder&) override;
  std::unique_ptr<darbotdb::PhysicalCollection> createPhysicalCollection(
      darbotdb::LogicalCollection& collection,
      darbotdb::velocypack::Slice /*info*/) override;
  darbotdb::Result createTickRanges(VPackBuilder&) override;
  std::unique_ptr<darbotdb::transaction::Manager> createTransactionManager(
      darbotdb::transaction::ManagerFeature&) override;
  std::shared_ptr<darbotdb::TransactionState> createTransactionState(
      TRI_vocbase_t& vocbase, darbotdb::TransactionId tid,
      darbotdb::transaction::Options const& options,
      darbotdb::transaction::OperationOrigin operationOrigin) override;
  darbotdb::Result createView(TRI_vocbase_t& vocbase, darbotdb::DataSourceId id,
                              darbotdb::LogicalView const& view) override;
  darbotdb::Result compactAll(bool changeLevels,
                              bool compactBottomMostLevel) override;
  TRI_voc_tick_t currentTick() const override;
  darbotdb::Result dropCollection(
      TRI_vocbase_t& vocbase, darbotdb::LogicalCollection& collection) override;
  darbotdb::Result dropDatabase(TRI_vocbase_t& vocbase) override;
  darbotdb::Result dropView(TRI_vocbase_t const& vocbase,
                            darbotdb::LogicalView const& view) override;
  darbotdb::Result firstTick(uint64_t&) override;
  std::vector<std::string> currentWalFiles() const override;
  darbotdb::Result flushWal(bool waitForSync, bool waitForCollector) override;
  void getCollectionInfo(TRI_vocbase_t& vocbase, darbotdb::DataSourceId cid,
                         darbotdb::velocypack::Builder& result,
                         bool includeIndexes, TRI_voc_tick_t maxTick) override;
  ErrorCode getCollectionsAndIndexes(TRI_vocbase_t& vocbase,
                                     darbotdb::velocypack::Builder& result,
                                     bool wasCleanShutdown,
                                     bool isUpgrade) override;
  void getDatabases(darbotdb::velocypack::Builder& result) override;
  void cleanupReplicationContexts() override;
  darbotdb::velocypack::Builder getReplicationApplierConfiguration(
      TRI_vocbase_t& vocbase, ErrorCode& result) override;
  darbotdb::velocypack::Builder getReplicationApplierConfiguration(
      ErrorCode& result) override;
  ErrorCode getViews(TRI_vocbase_t& vocbase,
                     darbotdb::velocypack::Builder& result) override;
  darbotdb::Result handleSyncKeys(darbotdb::DatabaseInitialSyncer& syncer,
                                  darbotdb::LogicalCollection& col,
                                  std::string const& keysId) override;
  darbotdb::RecoveryState recoveryState() override;
  TRI_voc_tick_t recoveryTick() override;

  darbotdb::Result lastLogger(
      TRI_vocbase_t& vocbase, uint64_t tickStart, uint64_t tickEnd,
      darbotdb::velocypack::Builder& builderSPtr) override;

  std::unique_ptr<TRI_vocbase_t> openDatabase(darbotdb::CreateDatabaseInfo&&,
                                              bool isUpgrade) override;
  using StorageEngine::registerCollection;
  using StorageEngine::registerView;
  TRI_voc_tick_t releasedTick() const override;
  void releaseTick(TRI_voc_tick_t) override;
  ErrorCode removeReplicationApplierConfiguration(
      TRI_vocbase_t& vocbase) override;
  ErrorCode removeReplicationApplierConfiguration() override;
  darbotdb::Result renameCollection(
      TRI_vocbase_t& vocbase, darbotdb::LogicalCollection const& collection,
      std::string const& oldName) override;
  ErrorCode saveReplicationApplierConfiguration(
      TRI_vocbase_t& vocbase, darbotdb::velocypack::Slice slice,
      bool doSync) override;
  ErrorCode saveReplicationApplierConfiguration(darbotdb::velocypack::Slice,
                                                bool) override;
  std::string versionFilename(TRI_voc_tick_t) const override;
  void waitForEstimatorSync() override;
  darbotdb::WalAccess const* walAccess() const override;

  bool autoRefillIndexCaches() const override;
  bool autoRefillIndexCachesOnFollowers() const override;

  static std::shared_ptr<darbotdb::iresearch::IResearchLinkMock> buildLinkMock(
      darbotdb::IndexId id, darbotdb::LogicalCollection& collection,
      VPackSlice const& info);

  static std::shared_ptr<darbotdb::iresearch::IResearchInvertedIndexMock>
  buildInvertedIndexMock(darbotdb::IndexId id,
                         darbotdb::LogicalCollection& collection,
                         VPackSlice const& info);

  auto dropReplicatedState(
      TRI_vocbase_t& vocbase,
      std::unique_ptr<darbotdb::replication2::storage::IStorageEngineMethods>&
          ptr) -> darbotdb::Result override;
  auto createReplicatedState(
      TRI_vocbase_t& vocbase, darbotdb::replication2::LogId id,
      const darbotdb::replication2::storage::PersistedStateInfo& info)
      -> darbotdb::ResultT<std::unique_ptr<
          darbotdb::replication2::storage::IStorageEngineMethods>> override;

  std::shared_ptr<darbotdb::StorageSnapshot> currentSnapshot() override {
    return std::make_shared<StorageEngineMockSnapshot>(currentTick());
  }

  void incrementTick(uint64_t tick) { _engineTick.fetch_add(tick); }

 private:
  TRI_voc_tick_t _releasedTick;
  std::atomic_uint64_t _engineTick{100};
  darbotdb::VersionTracker _versionTracker;
};
