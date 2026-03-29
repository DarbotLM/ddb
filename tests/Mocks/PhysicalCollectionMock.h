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
#include "StorageEngine/PhysicalCollection.h"

#include <atomic>
#include <string_view>

class PhysicalCollectionMock : public darbotdb::PhysicalCollection {
 public:
  struct DocElement {
    DocElement(std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> data,
               uint64_t docId);

    darbotdb::velocypack::Slice data() const;
    std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> rawData() const;
    darbotdb::LocalDocumentId docId() const;
    uint8_t const* vptr() const;
    void swapBuffer(
        std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>& newData);

   private:
    std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> _data;
    uint64_t const _docId;
  };

  static std::function<void()> before;

  PhysicalCollectionMock(darbotdb::LogicalCollection& collection);
  darbotdb::futures::Future<std::shared_ptr<darbotdb::Index>> createIndex(
      darbotdb::velocypack::Slice info, bool restore, bool& created,
      std::shared_ptr<std::function<darbotdb::Result(double)>> = nullptr,
      Replication2Callback replicationCb = nullptr) override;
  void deferDropCollection(
      std::function<bool(darbotdb::LogicalCollection&)> const& callback)
      override;
  darbotdb::Result dropIndex(darbotdb::IndexId iid) override;
  void figuresSpecific(bool details, darbotdb::velocypack::Builder&) override;
  std::unique_ptr<darbotdb::IndexIterator> getAllIterator(
      darbotdb::transaction::Methods* trx,
      darbotdb::ReadOwnWrites readOwnWrites) const override;
  std::unique_ptr<darbotdb::IndexIterator> getAnyIterator(
      darbotdb::transaction::Methods* trx) const override;
  std::unique_ptr<darbotdb::ReplicationIterator> getReplicationIterator(
      darbotdb::ReplicationIterator::Ordering, uint64_t) override;
  void getPropertiesVPack(darbotdb::velocypack::Builder&) const override;
  darbotdb::Result insert(darbotdb::transaction::Methods& trx,
                          darbotdb::IndexesSnapshot const& indexesSnapshot,
                          darbotdb::RevisionId newRevisionId,
                          darbotdb::velocypack::Slice newDocument,
                          darbotdb::OperationOptions const& options) override;

  darbotdb::Result lookupKey(
      darbotdb::transaction::Methods*, std::string_view,
      std::pair<darbotdb::LocalDocumentId, darbotdb::RevisionId>&,
      darbotdb::ReadOwnWrites) const override;
  darbotdb::Result lookupKeyForUpdate(
      darbotdb::transaction::Methods*, std::string_view,
      std::pair<darbotdb::LocalDocumentId, darbotdb::RevisionId>&)
      const override;
  uint64_t numberDocuments(darbotdb::transaction::Methods* trx) const override;
  void prepareIndexes(darbotdb::velocypack::Slice indexesSlice) override;

  darbotdb::IndexEstMap clusterIndexEstimates(
      bool allowUpdating, darbotdb::TransactionId tid) override;

  darbotdb::Result lookup(darbotdb::transaction::Methods* trx,
                          std::string_view key,
                          darbotdb::IndexIterator::DocumentCallback const& cb,
                          LookupOptions options) const final;
  darbotdb::Result lookup(
      darbotdb::transaction::Methods* trx, darbotdb::LocalDocumentId token,
      darbotdb::IndexIterator::DocumentCallback const& cb,
      LookupOptions options,
      darbotdb::StorageSnapshot const* snapshot) const final;
  darbotdb::Result lookup(darbotdb::transaction::Methods* trx,
                          std::span<darbotdb::LocalDocumentId> tokens,
                          MultiDocumentCallback const& cb,
                          LookupOptions options) const final;
  darbotdb::Result remove(darbotdb::transaction::Methods& trx,
                          darbotdb::IndexesSnapshot const& indexesSnapshot,
                          darbotdb::LocalDocumentId previousDocumentId,
                          darbotdb::RevisionId previousRevisionId,
                          darbotdb::velocypack::Slice previousDocument,
                          darbotdb::OperationOptions const& options) override;
  darbotdb::Result replace(darbotdb::transaction::Methods& trx,
                           darbotdb::IndexesSnapshot const& indexesSnapshot,
                           darbotdb::LocalDocumentId newDocumentId,
                           darbotdb::RevisionId previousRevisionId,
                           darbotdb::velocypack::Slice previousDocument,
                           darbotdb::RevisionId newRevisionId,
                           darbotdb::velocypack::Slice newDocument,
                           darbotdb::OperationOptions const& options) override;
  darbotdb::RevisionId revision(
      darbotdb::transaction::Methods* trx) const override;
  darbotdb::Result truncate(darbotdb::transaction::Methods& trx,
                            darbotdb::OperationOptions& options,
                            bool& usedRangeDelete) override;
  void compact() override {}
  darbotdb::Result update(darbotdb::transaction::Methods& trx,
                          darbotdb::IndexesSnapshot const& indexesSnapshot,
                          darbotdb::LocalDocumentId newDocumentId,
                          darbotdb::RevisionId previousRevisionId,
                          darbotdb::velocypack::Slice previousDocument,
                          darbotdb::RevisionId newRevisionId,
                          darbotdb::velocypack::Slice newDocument,
                          darbotdb::OperationOptions const& options) override;
  darbotdb::Result updateProperties(darbotdb::velocypack::Slice slice) override;

  bool cacheEnabled() const noexcept override { return false; }

 private:
  bool addIndex(std::shared_ptr<darbotdb::Index> idx);

  darbotdb::Result updateInternal(darbotdb::transaction::Methods& trx,
                                  darbotdb::LocalDocumentId newDocumentId,
                                  darbotdb::RevisionId previousRevisionId,
                                  darbotdb::velocypack::Slice previousDocument,
                                  darbotdb::RevisionId newRevisionId,
                                  darbotdb::velocypack::Slice newDocument,
                                  darbotdb::OperationOptions const& options,
                                  bool isUpdate);

  uint64_t _lastDocumentId;
  // map _key => data. Keyslice references memory in the value
  std::unordered_map<std::string_view, DocElement> _documents;
};
