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
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#include "RocksDBTypes.h"

using namespace darbotdb;

/// @brief rocksdb format version
char darbotdb::rocksDBFormatVersion() { return '1'; }

std::string_view darbotdb::rocksDBEndiannessString(RocksDBEndianness value) {
  switch (value) {
    case RocksDBEndianness::Big:
      return "big";
    case RocksDBEndianness::Little:
      return "little";
    case RocksDBEndianness::Invalid:
      break;
  }
  return "invalid";
}

namespace {

static RocksDBEntryType placeholder = darbotdb::RocksDBEntryType::Placeholder;
static rocksdb::Slice Placeholder(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &placeholder),
    1);

static RocksDBEntryType database = darbotdb::RocksDBEntryType::Database;
static rocksdb::Slice Database(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(&database),
    1);

static RocksDBEntryType collection = RocksDBEntryType::Collection;
static rocksdb::Slice Collection(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &collection),
    1);

static RocksDBEntryType counterVal = RocksDBEntryType::CounterValue;
static rocksdb::Slice CounterValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &counterVal),
    1);

static RocksDBEntryType document = RocksDBEntryType::Document;
static rocksdb::Slice Document(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(&document),
    1);

static RocksDBEntryType primaryIndexValue = RocksDBEntryType::PrimaryIndexValue;
static rocksdb::Slice PrimaryIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &primaryIndexValue),
    1);

static RocksDBEntryType edgeIndexValue = RocksDBEntryType::EdgeIndexValue;
static rocksdb::Slice EdgeIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &edgeIndexValue),
    1);

static RocksDBEntryType vpackIndexValue = RocksDBEntryType::VPackIndexValue;
static rocksdb::Slice VPackIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &vpackIndexValue),
    1);

static RocksDBEntryType uniqueVPIndex = RocksDBEntryType::UniqueVPackIndexValue;
static rocksdb::Slice UniqueVPackIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &uniqueVPIndex),
    1);

static RocksDBEntryType vectorVPIndex = RocksDBEntryType::VectorVPackIndexValue;
static rocksdb::Slice VectorVPackIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &vectorVPIndex),
    1);

static RocksDBEntryType fulltextIndexValue =
    RocksDBEntryType::FulltextIndexValue;
static rocksdb::Slice FulltextIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &fulltextIndexValue),
    1);

static RocksDBEntryType geoIndexValue = RocksDBEntryType::LegacyGeoIndexValue;
static rocksdb::Slice LegacyGeoIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &geoIndexValue),
    1);

static RocksDBEntryType s2IndexValue = RocksDBEntryType::GeoIndexValue;
static rocksdb::Slice GeoIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &s2IndexValue),
    1);

static RocksDBEntryType view = RocksDBEntryType::View;
static rocksdb::Slice View(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(&view), 1);

static RocksDBEntryType settingsValue = RocksDBEntryType::SettingsValue;
static rocksdb::Slice SettingsValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &settingsValue),
    1);

static RocksDBEntryType replicationApplierConfig =
    RocksDBEntryType::ReplicationApplierConfig;
static rocksdb::Slice ReplicationApplierConfig(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &replicationApplierConfig),
    1);

static RocksDBEntryType indexEstimateValue =
    RocksDBEntryType::IndexEstimateValue;
static rocksdb::Slice IndexEstimateValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &indexEstimateValue),
    1);

static RocksDBEntryType keyGeneratorValue = RocksDBEntryType::KeyGeneratorValue;
static rocksdb::Slice KeyGeneratorValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &keyGeneratorValue),
    1);

static RocksDBEntryType logEntry = RocksDBEntryType::LogEntry;
static rocksdb::Slice LogEntry(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(&logEntry),
    1);

static RocksDBEntryType replicatedState = RocksDBEntryType::ReplicatedState;
static rocksdb::Slice ReplicatedState(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &replicatedState),
    1);

static RocksDBEntryType revisionTreeValue = RocksDBEntryType::RevisionTreeValue;
static rocksdb::Slice RevisionTreeValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &revisionTreeValue),
    1);

static RocksDBEntryType mdiIndexValue = RocksDBEntryType::MdiIndexValue;
static rocksdb::Slice MdiIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &mdiIndexValue),
    1);

static RocksDBEntryType uniqueMdiIndexValue =
    RocksDBEntryType::UniqueMdiIndexValue;
static rocksdb::Slice UniqueMdiIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &uniqueMdiIndexValue),
    1);

static RocksDBEntryType mdiVPackIndexValue =
    RocksDBEntryType::MdiVPackIndexValue;
static rocksdb::Slice MdiVPackIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &mdiVPackIndexValue),
    1);

static RocksDBEntryType uniqueMdiVPackIndexValue =
    RocksDBEntryType::UniqueMdiVPackIndexValue;
static rocksdb::Slice UniqueMdiVPackIndexValue(
    reinterpret_cast<std::underlying_type<RocksDBEntryType>::type*>(
        &uniqueMdiVPackIndexValue),
    1);
}  // namespace

std::string_view darbotdb::rocksDBEntryTypeName(
    darbotdb::RocksDBEntryType type) {
  switch (type) {
    case darbotdb::RocksDBEntryType::Placeholder:
      return "Placeholder";
    case darbotdb::RocksDBEntryType::Database:
      return "Database";
    case darbotdb::RocksDBEntryType::Collection:
      return "Collection";
    case darbotdb::RocksDBEntryType::CounterValue:
      return "CounterValue";
    case darbotdb::RocksDBEntryType::Document:
      return "Document";
    case darbotdb::RocksDBEntryType::PrimaryIndexValue:
      return "PrimaryIndexValue";
    case darbotdb::RocksDBEntryType::EdgeIndexValue:
      return "EdgeIndexValue";
    case darbotdb::RocksDBEntryType::VPackIndexValue:
      return "VPackIndexValue";
    case darbotdb::RocksDBEntryType::UniqueVPackIndexValue:
      return "UniqueVPackIndexValue";
    case darbotdb::RocksDBEntryType::View:
      return "View";
    case darbotdb::RocksDBEntryType::SettingsValue:
      return "SettingsValue";
    case darbotdb::RocksDBEntryType::ReplicationApplierConfig:
      return "ReplicationApplierConfig";
    case darbotdb::RocksDBEntryType::FulltextIndexValue:
      return "FulltextIndexValue";
    case darbotdb::RocksDBEntryType::LegacyGeoIndexValue:
      return "LegacyGeoIndexValue";
    case darbotdb::RocksDBEntryType::GeoIndexValue:
      return "SphericalIndexValue";
    case darbotdb::RocksDBEntryType::IndexEstimateValue:
      return "IndexEstimateValue";
    case darbotdb::RocksDBEntryType::KeyGeneratorValue:
      return "KeyGeneratorValue";
    case darbotdb::RocksDBEntryType::RevisionTreeValue:
      return "RevisionTreeValue";
    case darbotdb::RocksDBEntryType::MdiIndexValue:
      return "MdiIndexValue";
    case darbotdb::RocksDBEntryType::UniqueMdiIndexValue:
      return "UniqueMdiIndexValue";
    case darbotdb::RocksDBEntryType::MdiVPackIndexValue:
      return "MdiVPackIndexValue";
    case darbotdb::RocksDBEntryType::UniqueMdiVPackIndexValue:
      return "UniqueMdiVPackIndexValue";
    case darbotdb::RocksDBEntryType::VectorVPackIndexValue:
      return "VectorVPackIndexValue";
    case RocksDBEntryType::LogEntry:
      return "ReplicatedLogEntry";
    case RocksDBEntryType::ReplicatedState:
      return "ReplicatedState";
  }
  return "Invalid";
}

std::string_view darbotdb::rocksDBLogTypeName(darbotdb::RocksDBLogType type) {
  switch (type) {
    case darbotdb::RocksDBLogType::DatabaseCreate:
      return "DatabaseCreate";
    case darbotdb::RocksDBLogType::DatabaseDrop:
      return "DatabaseDrop";
    case darbotdb::RocksDBLogType::CollectionCreate:
      return "CollectionCreate";
    case darbotdb::RocksDBLogType::CollectionDrop:
      return "CollectionDrop";
    case darbotdb::RocksDBLogType::CollectionRename:
      return "CollectionRename";
    case darbotdb::RocksDBLogType::CollectionChange:
      return "CollectionChange";
    case darbotdb::RocksDBLogType::CollectionTruncate:
      return "CollectionTruncate";
    case darbotdb::RocksDBLogType::IndexCreate:
      return "IndexCreate";
    case darbotdb::RocksDBLogType::IndexDrop:
      return "IndexDrop";
    case darbotdb::RocksDBLogType::ViewCreate:
      return "ViewCreate";
    case darbotdb::RocksDBLogType::ViewDrop:
      return "ViewDrop";
    case darbotdb::RocksDBLogType::ViewChange:
      return "ViewChange";
    case darbotdb::RocksDBLogType::BeginTransaction:
      return "BeginTransaction";
    case darbotdb::RocksDBLogType::CommitTransaction:
      return "CommitTransaction";
    case darbotdb::RocksDBLogType::DocumentOperationsPrologue:
      return "DocumentOperationsPrologue";
    case darbotdb::RocksDBLogType::DocumentRemove:
      return "DocumentRemove";
    case darbotdb::RocksDBLogType::DocumentRemoveV2:
      return "DocumentRemoveV2";
    case darbotdb::RocksDBLogType::DocumentRemoveAsPartOfUpdate:
      return "IgnoreRemoveAsPartOfUpdate";
    case darbotdb::RocksDBLogType::SinglePut:
      return "SinglePut";
    case darbotdb::RocksDBLogType::SingleRemove:
      return "SingleRemove";
    case darbotdb::RocksDBLogType::SingleRemoveV2:
      return "SingleRemoveV2";
    case darbotdb::RocksDBLogType::FlushSync:
      return "FlushSync";
    case RocksDBLogType::TrackedDocumentInsert:
      return "TrackedDocumentInsert";
    case RocksDBLogType::TrackedDocumentRemove:
      return "TrackedDocumentRemove";
    case darbotdb::RocksDBLogType::Invalid:
      return "Invalid";
  }
  return "Invalid";
}

rocksdb::Slice const& darbotdb::rocksDBSlice(RocksDBEntryType const& type) {
  switch (type) {
    case RocksDBEntryType::Placeholder:
      return Placeholder;
    case RocksDBEntryType::Database:
      return Database;
    case RocksDBEntryType::Collection:
      return Collection;
    case RocksDBEntryType::CounterValue:
      return CounterValue;
    case RocksDBEntryType::Document:
      return Document;
    case RocksDBEntryType::PrimaryIndexValue:
      return PrimaryIndexValue;
    case RocksDBEntryType::EdgeIndexValue:
      return EdgeIndexValue;
    case RocksDBEntryType::VPackIndexValue:
      return VPackIndexValue;
    case RocksDBEntryType::UniqueVPackIndexValue:
      return UniqueVPackIndexValue;
    case darbotdb::RocksDBEntryType::VectorVPackIndexValue:
      return VectorVPackIndexValue;
    case RocksDBEntryType::FulltextIndexValue:
      return FulltextIndexValue;
    case RocksDBEntryType::LegacyGeoIndexValue:
      return LegacyGeoIndexValue;
    case RocksDBEntryType::GeoIndexValue:
      return GeoIndexValue;
    case RocksDBEntryType::View:
      return View;
    case RocksDBEntryType::SettingsValue:
      return SettingsValue;
    case RocksDBEntryType::ReplicationApplierConfig:
      return ReplicationApplierConfig;
    case RocksDBEntryType::IndexEstimateValue:
      return IndexEstimateValue;
    case RocksDBEntryType::KeyGeneratorValue:
      return KeyGeneratorValue;
    case RocksDBEntryType::RevisionTreeValue:
      return RevisionTreeValue;
    case RocksDBEntryType::MdiIndexValue:
      return MdiIndexValue;
    case RocksDBEntryType::UniqueMdiIndexValue:
      return UniqueMdiIndexValue;
    case RocksDBEntryType::MdiVPackIndexValue:
      return MdiVPackIndexValue;
    case RocksDBEntryType::UniqueMdiVPackIndexValue:
      return UniqueMdiVPackIndexValue;
    case RocksDBEntryType::LogEntry:
      return LogEntry;
    case RocksDBEntryType::ReplicatedState:
      return ReplicatedState;
  }

  return Placeholder;  // avoids warning - errorslice instead ?!
}
