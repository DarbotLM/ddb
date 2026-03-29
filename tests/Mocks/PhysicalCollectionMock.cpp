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

#include "PhysicalCollectionMock.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "Basics/DownCast.h"
#include "Basics/StaticStrings.h"
#include "Basics/VelocyPackHelper.h"
#include "ClusterEngine/ClusterEngine.h"
#include "Indexes/SimpleAttributeEqualityMatcher.h"
#include "Indexes/SortedIndexAttributeMatcher.h"
#include "IResearch/IResearchCommon.h"
#include "IResearch/IResearchFeature.h"
#include "Logger/LogMacros.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "Transaction/Helpers.h"
#include "Transaction/OperationOrigin.h"
#include "Transaction/StandaloneContext.h"
#include "Utils/SingleCollectionTransaction.h"
#include "VocBase/LogicalCollection.h"

#include "Mocks/IResearchLinkMock.h"
#include "Mocks/IResearchInvertedIndexMock.h"
#include "Mocks/StorageEngineMock.h"

#include <velocypack/Iterator.h>

namespace {

darbotdb::LocalDocumentId generateDocumentId(
    darbotdb::LogicalCollection const& collection,
    darbotdb::RevisionId revisionId, uint64_t& documentId) {
  bool useRev = collection.usesRevisionsAsDocumentIds();
  return useRev ? darbotdb::LocalDocumentId::create(revisionId)
                : darbotdb::LocalDocumentId::create(++documentId);
}

/// @brief hard-coded vector of the index attributes
/// note that the attribute names must be hard-coded here to avoid an init-order
/// fiasco with StaticStrings::FromString etc.
std::vector<std::vector<darbotdb::basics::AttributeName>> const IndexAttributes{
    {darbotdb::basics::AttributeName(std::string_view("_from"), false)},
    {darbotdb::basics::AttributeName(std::string_view("_to"), false)}};

/// @brief add a single value node to the iterator's keys
void handleValNode(VPackBuilder* keys, darbotdb::aql::AstNode const* valNode) {
  if (!valNode->isStringValue() || valNode->getStringLength() == 0) {
    return;
  }

  keys->openObject();
  keys->add(darbotdb::StaticStrings::IndexEq,
            VPackValuePair(valNode->getStringValue(),
                           valNode->getStringLength(), VPackValueType::String));
  keys->close();

  TRI_IF_FAILURE("EdgeIndex::collectKeys") {
    THROW_DDB_EXCEPTION(TRI_ERROR_DEBUG);
  }
}

class EdgeIndexIteratorMock final : public darbotdb::IndexIterator {
 public:
  typedef std::unordered_multimap<std::string, darbotdb::LocalDocumentId> Map;

  EdgeIndexIteratorMock(darbotdb::LogicalCollection* collection,
                        darbotdb::transaction::Methods* trx,
                        darbotdb::Index const* index, Map const& map,
                        std::unique_ptr<VPackBuilder>&& keys, bool isFrom)
      : IndexIterator(collection, trx, darbotdb::ReadOwnWrites::no),
        _map(map),
        _begin(_map.end()),
        _end(_map.end()),
        _keys(std::move(keys)),
        _keysIt(_keys->slice()) {}

  bool prepareNextRange() {
    if (_keysIt.valid()) {
      auto key = _keysIt.value();

      if (key.isObject()) {
        key = key.get(darbotdb::StaticStrings::IndexEq);
      }

      std::tie(_begin, _end) = _map.equal_range(key.toString());
      ++_keysIt;
      return true;
    } else {
      // Just make sure begin and end are equal
      _begin = _map.end();
      _end = _map.end();
      return false;
    }
  }

  std::string_view typeName() const noexcept final {
    return "edge-index-iterator-mock";
  }

  bool nextImpl(LocalDocumentIdCallback const& cb, uint64_t limit) override {
    // We can at most return limit
    for (uint64_t l = 0; l < limit; ++l) {
      while (_begin == _end) {
        if (!prepareNextRange()) {
          return false;
        }
      }
      TRI_ASSERT(_begin != _end);
      cb(_begin->second);
      ++_begin;
    }
    // Returned due to limit.
    if (_begin == _end) {
      // Out limit hit the last index entry
      // Return false if we do not have another range
      return prepareNextRange();
    }
    return true;
  }

  void resetImpl() override {
    _keysIt.reset();
    _begin = _map.begin();
    _end = _map.end();
  }

 private:
  Map const& _map;
  Map::const_iterator _begin;
  Map::const_iterator _end;
  std::unique_ptr<VPackBuilder> _keys;
  darbotdb::velocypack::ArrayIterator _keysIt;
};  // EdgeIndexIteratorMock

class AllIteratorMock final : public darbotdb::IndexIterator {
 public:
  AllIteratorMock(
      std::unordered_map<std::string_view,
                         PhysicalCollectionMock::DocElement> const& data,
      darbotdb::LogicalCollection& coll, darbotdb::transaction::Methods* trx,
      darbotdb::ReadOwnWrites readOwnWrites)
      : darbotdb::IndexIterator(&coll, trx, readOwnWrites),
        _data(data),
        _ref(readOwnWrites == darbotdb::ReadOwnWrites::yes ? data : _data),
        _it{_ref.begin()} {}

  std::string_view typeName() const noexcept final { return "AllIteratorMock"; }

  void resetImpl() override { _it = _ref.begin(); }

  bool nextImpl(LocalDocumentIdCallback const& callback,
                uint64_t limit) override {
    while (_it != _ref.end() && limit != 0) {
      callback(_it->second.docId());
      ++_it;
      --limit;
    }
    return 0 == limit;
  }

 private:
  // we need to take a copy of the incoming data here, so we can iterate over
  // the original data safely while the collection data is being modified
  std::unordered_map<std::string_view, PhysicalCollectionMock::DocElement>
      _data;
  // we also need to keep a reference to the original map in order to satisfy
  // read-your-own-writes queries
  std::unordered_map<std::string_view,
                     PhysicalCollectionMock::DocElement> const& _ref;
  std::unordered_map<std::string_view,
                     PhysicalCollectionMock::DocElement>::const_iterator _it;
};  // AllIteratorMock

class EdgeIndexMock final : public darbotdb::Index {
 public:
  static std::shared_ptr<darbotdb::Index> make(
      darbotdb::IndexId iid, darbotdb::LogicalCollection& collection,
      darbotdb::velocypack::Slice const& definition) {
    auto const typeSlice = definition.get("type");

    if (typeSlice.isNone()) {
      return nullptr;
    }

    auto const type = darbotdb::basics::VelocyPackHelper::getStringView(
        typeSlice, std::string_view());

    if (type.compare("edge") != 0) {
      return nullptr;
    }

    return std::make_shared<EdgeIndexMock>(iid, collection);
  }

  IndexType type() const override { return Index::TRI_IDX_TYPE_EDGE_INDEX; }

  char const* typeName() const override { return "edge"; }

  std::vector<std::vector<darbotdb::basics::AttributeName>> const&
  coveredFields() const override {
    // index does not cover the index attribute!
    return Index::emptyCoveredFields;
  }

  bool canBeDropped() const override { return false; }

  bool isHidden() const override { return false; }

  bool isSorted() const override { return false; }

  bool hasSelectivityEstimate() const override { return false; }

  size_t memory() const override { return sizeof(EdgeIndexMock); }

  void load() override {}
  void unload() override {}

  void toVelocyPack(VPackBuilder& builder,
                    std::underlying_type<darbotdb::Index::Serialize>::type
                        flags) const override {
    builder.openObject();
    Index::toVelocyPack(builder, flags);
    // hard-coded
    builder.add("unique", VPackValue(false));
    builder.add("sparse", VPackValue(false));
    builder.close();
  }

  void toVelocyPackFigures(VPackBuilder& builder) const override {
    Index::toVelocyPackFigures(builder);

    builder.add("from", VPackValue(VPackValueType::Object));
    //_edgesFrom->appendToVelocyPack(builder);
    builder.close();

    builder.add("to", VPackValue(VPackValueType::Object));
    //_edgesTo->appendToVelocyPack(builder);
    builder.close();
  }

  darbotdb::Result insert(darbotdb::transaction::Methods& trx,
                          darbotdb::LocalDocumentId documentId,
                          darbotdb::velocypack::Slice doc) {
    if (!doc.isObject()) {
      return {TRI_ERROR_INTERNAL};
    }

    VPackSlice const fromValue(
        darbotdb::transaction::helpers::extractFromFromDocument(doc));

    if (!fromValue.isString()) {
      return {TRI_ERROR_INTERNAL};
    }

    VPackSlice const toValue(
        darbotdb::transaction::helpers::extractToFromDocument(doc));

    if (!toValue.isString()) {
      return {TRI_ERROR_INTERNAL};
    }

    _edgesFrom.emplace(fromValue.toString(), documentId);
    _edgesTo.emplace(toValue.toString(), documentId);

    return {};  // ok
  }

  darbotdb::Result remove(darbotdb::transaction::Methods& trx,
                          darbotdb::LocalDocumentId,
                          darbotdb::velocypack::Slice doc,
                          darbotdb::IndexOperationMode) {
    if (!doc.isObject()) {
      return {TRI_ERROR_INTERNAL};
    }

    VPackSlice const fromValue(
        darbotdb::transaction::helpers::extractFromFromDocument(doc));

    if (!fromValue.isString()) {
      return {TRI_ERROR_INTERNAL};
    }

    VPackSlice const toValue(
        darbotdb::transaction::helpers::extractToFromDocument(doc));

    if (!toValue.isString()) {
      return {TRI_ERROR_INTERNAL};
    }

    _edgesFrom.erase(fromValue.toString());
    _edgesTo.erase(toValue.toString());

    return {};  // ok
  }

  Index::FilterCosts supportsFilterCondition(
      darbotdb::transaction::Methods& /*trx*/,
      std::vector<std::shared_ptr<darbotdb::Index>> const& /*allIndexes*/,
      darbotdb::aql::AstNode const* node,
      darbotdb::aql::Variable const* reference,
      size_t itemsInIndex) const override {
    darbotdb::SimpleAttributeEqualityMatcher matcher(IndexAttributes);
    return matcher.matchOne(this, node, reference, itemsInIndex);
  }

  std::unique_ptr<darbotdb::IndexIterator> iteratorForCondition(
      darbotdb::ResourceMonitor& monitor, darbotdb::transaction::Methods* trx,
      darbotdb::aql::AstNode const* node, darbotdb::aql::Variable const*,
      darbotdb::IndexIteratorOptions const&, darbotdb::ReadOwnWrites,
      int) override {
    TRI_ASSERT(node->type == darbotdb::aql::NODE_TYPE_OPERATOR_NARY_AND);

    TRI_ASSERT(node->numMembers() == 1);

    auto comp = node->getMember(0);

    // assume a.b == value
    auto attrNode = comp->getMember(0);
    auto valNode = comp->getMember(1);

    if (attrNode->type != darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS) {
      // got value == a.b  -> flip sides
      std::swap(attrNode, valNode);
    }
    TRI_ASSERT(attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS);

    if (comp->type == darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
      // a.b == value
      return createEqIterator(trx, attrNode, valNode);
    }

    if (comp->type == darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
      // a.b IN values
      if (!valNode->isArray()) {
        // a.b IN non-array
        return std::make_unique<darbotdb::EmptyIndexIterator>(&_collection,
                                                              trx);
      }

      return createInIterator(trx, attrNode, valNode);
    }

    // operator type unsupported
    return std::make_unique<darbotdb::EmptyIndexIterator>(&_collection, trx);
  }

  darbotdb::aql::AstNode* specializeCondition(
      darbotdb::transaction::Methods& /*trx*/, darbotdb::aql::AstNode* node,
      darbotdb::aql::Variable const* reference) const override {
    darbotdb::SimpleAttributeEqualityMatcher matcher(IndexAttributes);

    return matcher.specializeOne(this, node, reference);
  }

  EdgeIndexMock(darbotdb::IndexId iid, darbotdb::LogicalCollection& collection)
      : darbotdb::Index(iid, collection, darbotdb::StaticStrings::IndexNameEdge,
                        {{darbotdb::basics::AttributeName(
                             darbotdb::StaticStrings::FromString, false)},
                         {darbotdb::basics::AttributeName(
                             darbotdb::StaticStrings::ToString, false)}},
                        true, false) {}

  std::unique_ptr<darbotdb::IndexIterator> createEqIterator(
      darbotdb::transaction::Methods* trx,
      darbotdb::aql::AstNode const* attrNode,
      darbotdb::aql::AstNode const* valNode) const {
    // lease builder, but immediately pass it to the unique_ptr so we don't leak
    darbotdb::transaction::BuilderLeaser builder(trx);
    std::unique_ptr<VPackBuilder> keys(builder.steal());
    keys->openArray();

    handleValNode(keys.get(), valNode);
    TRI_IF_FAILURE("EdgeIndex::noIterator") {
      THROW_DDB_EXCEPTION(TRI_ERROR_DEBUG);
    }
    keys->close();

    // _from or _to?
    bool const isFrom =
        (attrNode->stringEquals(darbotdb::StaticStrings::FromString));

    return std::make_unique<EdgeIndexIteratorMock>(
        &_collection, trx, this, isFrom ? _edgesFrom : _edgesTo,
        std::move(keys), isFrom);
  }

  /// @brief create the iterator
  std::unique_ptr<darbotdb::IndexIterator> createInIterator(
      darbotdb::transaction::Methods* trx,
      darbotdb::aql::AstNode const* attrNode,
      darbotdb::aql::AstNode const* valNode) const {
    // lease builder, but immediately pass it to the unique_ptr so we don't leak
    darbotdb::transaction::BuilderLeaser builder(trx);
    std::unique_ptr<VPackBuilder> keys(builder.steal());
    keys->openArray();

    size_t const n = valNode->numMembers();
    for (size_t i = 0; i < n; ++i) {
      handleValNode(keys.get(), valNode->getMemberUnchecked(i));
      TRI_IF_FAILURE("EdgeIndex::iteratorValNodes") {
        THROW_DDB_EXCEPTION(TRI_ERROR_DEBUG);
      }
    }

    TRI_IF_FAILURE("EdgeIndex::noIterator") {
      THROW_DDB_EXCEPTION(TRI_ERROR_DEBUG);
    }
    keys->close();

    // _from or _to?
    bool const isFrom =
        (attrNode->stringEquals(darbotdb::StaticStrings::FromString));

    return std::make_unique<EdgeIndexIteratorMock>(
        &_collection, trx, this, isFrom ? _edgesFrom : _edgesTo,
        std::move(keys), isFrom);
  }

  /// @brief the hash table for _from
  EdgeIndexIteratorMock::Map _edgesFrom;
  /// @brief the hash table for _to
  EdgeIndexIteratorMock::Map _edgesTo;
};  // EdgeIndexMock

class HashIndexMap {
  struct VPackBuilderHasher {
    std::size_t operator()(VPackBuilder const& builder) const {
      return std::hash<VPackSlice>()(builder.slice());
    }
  };

  struct VPackBuilderComparator {
    bool operator()(VPackBuilder const& builder1,
                    VPackBuilder const& builder2) const {
      return ::darbotdb::basics::VelocyPackHelper::compare(
                 builder1.slice(), builder2.slice(), true) == 0;
    }
  };

  using ValueMap =
      std::unordered_multimap<VPackBuilder, darbotdb::LocalDocumentId,
                              VPackBuilderHasher, VPackBuilderComparator>;
  using DocumentsIndexMap =
      std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>;

  darbotdb::velocypack::Slice getSliceByField(
      darbotdb::velocypack::Slice const& doc, size_t i) {
    TRI_ASSERT(i < _fields.size());
    TRI_ASSERT(!doc.isNone());
    auto slice = doc;
    for (auto const& f : _fields[i]) {
      slice = slice.get(f.name);
      if (slice.isNone() || slice.isNull()) {
        break;
      }
    }
    return slice;
  }

  void insertSlice(darbotdb::LocalDocumentId documentId,
                   darbotdb::velocypack::Slice const& slice, size_t i) {
    VPackBuilder builder;
    if (slice.isNone() || slice.isNull()) {
      builder.add(VPackSlice::nullSlice());
    } else {
      builder.add(slice);
    }
    _valueMaps[i].emplace(std::move(builder), documentId);
  }

 public:
  HashIndexMap(
      std::vector<std::vector<darbotdb::basics::AttributeName>> const& fields)
      : _fields(fields), _valueMaps(fields.size()) {
    TRI_ASSERT(!_fields.empty());
  }

  void insert(darbotdb::LocalDocumentId documentId,
              darbotdb::velocypack::Slice const& doc) {
    VPackBuilder builder;
    builder.openArray();
    auto toClose = true;
    // find fields for the index
    for (size_t i = 0; i < _fields.size(); ++i) {
      auto slice = doc;
      auto isExpansion = false;
      for (auto fieldIt = _fields[i].begin(); fieldIt != _fields[i].end();
           ++fieldIt) {
        TRI_ASSERT(slice.isObject() || slice.isArray());
        if (slice.isObject()) {
          slice = slice.get(fieldIt->name);
          if ((fieldIt->shouldExpand && slice.isObject()) ||
              (!fieldIt->shouldExpand && slice.isArray())) {
            slice = VPackSlice::nullSlice();
            break;
          }
          if (slice.isNone() || slice.isNull()) {
            break;
          }
        } else {  // expansion
          isExpansion = slice.isArray();
          TRI_ASSERT(isExpansion);
          auto found = false;
          for (auto sliceIt = darbotdb::velocypack::ArrayIterator(slice);
               sliceIt != sliceIt.end(); ++sliceIt) {
            auto subSlice = sliceIt.value();
            if (!(subSlice.isNone() || subSlice.isNull())) {
              for (auto fieldItForArray = fieldIt;
                   fieldItForArray != _fields[i].end(); ++fieldItForArray) {
                TRI_ASSERT(subSlice.isObject());
                subSlice = subSlice.get(fieldItForArray->name);
                if (subSlice.isNone() || subSlice.isNull()) {
                  break;
                }
              }
              if (!(subSlice.isNone() || subSlice.isNull())) {
                insertSlice(documentId, subSlice, i);
                builder.add(subSlice);
                found = true;
                break;
              }
            }
          }
          if (!found) {
            insertSlice(documentId, VPackSlice::nullSlice(), i);
            builder.add(VPackSlice::nullSlice());
          }
          break;
        }
      }
      if (!isExpansion) {
        // if the last expansion (at the end) leave the array open
        if (slice.isArray() && i == _fields.size() - 1) {
          auto found = false;
          auto wasNull = false;
          for (auto sliceIt = darbotdb::velocypack::ArrayIterator(slice);
               sliceIt != sliceIt.end(); ++sliceIt) {
            auto subSlice = sliceIt.value();
            if (!(subSlice.isNone() || subSlice.isNull())) {
              insertSlice(documentId, subSlice, i);
              found = true;
            } else {
              wasNull = true;
            }
          }
          if (!found || wasNull) {
            insertSlice(documentId, VPackSlice::nullSlice(), i);
          }
          toClose = false;
        } else {  // object
          insertSlice(documentId, slice, i);
          builder.add(slice);
        }
      }
    }
    if (toClose) {
      builder.close();
    }
    _docIndexMap.try_emplace(documentId, std::move(builder));
  }

  bool remove(darbotdb::LocalDocumentId documentId,
              darbotdb::velocypack::Slice doc) {
    size_t i = 0;
    auto documentRemoved = false;
    for (auto& map : _valueMaps) {
      auto slice = getSliceByField(doc, i++);
      auto [begin, end] = map.equal_range(VPackBuilder(slice));
      for (; begin != end; ++begin) {
        if (begin->second == documentId) {
          map.erase(begin);
          documentRemoved = true;
          // not break because of expansions
        }
      }
    }
    _docIndexMap.erase(documentId);
    return documentRemoved;
  }

  void clear() {
    _valueMaps.clear();
    _docIndexMap.clear();
  }

  std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder> find(
      std::unique_ptr<VPackBuilder>&& keys) const {
    std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder const*> found;
    TRI_ASSERT(keys->slice().isArray());
    auto sliceIt = darbotdb::velocypack::ArrayIterator(keys->slice());
    if (!sliceIt.valid()) {
      return std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>();
    }
    for (auto const& map : _valueMaps) {
      auto [begin, end] = map.equal_range(VPackBuilder(sliceIt.value()));
      if (begin == end) {
        return std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>();
      }
      if (found.empty()) {
        std::transform(begin, end, std::inserter(found, found.end()),
                       [](auto const& item) {
                         return std::make_pair(item.second, &item.first);
                       });
      } else {
        std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder const*>
            tmpFound;
        for (; begin != end; ++begin) {
          if (found.find(begin->second) != found.cend()) {
            tmpFound.try_emplace(begin->second, &begin->first);
          }
        }
        if (tmpFound.empty()) {
          return std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>();
        }
        found.swap(tmpFound);
      }
      if (!(++sliceIt).valid()) {
        break;
      }
    }
    std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>
        foundWithCovering;
    for (auto const& d : found) {
      auto doc = _docIndexMap.find(d.first);
      TRI_ASSERT(doc != _docIndexMap.cend());
      auto builder = doc->second;
      // the array was left open for the last expansion (at the end)
      if (doc->second.isOpenArray()) {
        builder.add(d.second->slice());
        builder.close();
      }
      foundWithCovering.try_emplace(doc->first, std::move(builder));
    }
    return foundWithCovering;
  }

 private:
  std::vector<std::vector<darbotdb::basics::AttributeName>> const& _fields;
  std::vector<ValueMap> _valueMaps;
  DocumentsIndexMap _docIndexMap;
};

class HashIndexIteratorMock final : public darbotdb::IndexIterator {
 public:
  HashIndexIteratorMock(darbotdb::LogicalCollection* collection,
                        darbotdb::transaction::Methods* trx,
                        darbotdb::Index const* index, HashIndexMap const& map,
                        std::unique_ptr<VPackBuilder>&& keys)
      : IndexIterator(collection, trx, darbotdb::ReadOwnWrites::no), _map(map) {
    _documents = _map.find(std::move(keys));
    _begin = _documents.begin();
    _end = _documents.end();
  }

  std::string_view typeName() const noexcept final {
    return "hash-index-iterator-mock";
  }

  bool nextCoveringImpl(CoveringCallback const& cb, uint64_t limit) override {
    while (limit && _begin != _end) {
      auto data = SliceCoveringData(_begin->second.slice());
      cb(_begin->first, data);
      ++_begin;
      --limit;
    }

    return _begin != _end;
  }

  bool nextImpl(LocalDocumentIdCallback const& cb, uint64_t limit) override {
    while (limit && _begin != _end) {
      cb(_begin->first);
      ++_begin;
      --limit;
    }

    return _begin != _end;
  }

  void resetImpl() override {
    _documents.clear();
    _begin = _documents.begin();
    _end = _documents.end();
  }

 private:
  HashIndexMap const& _map;
  std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder> _documents;
  std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>::const_iterator
      _begin;
  std::unordered_map<darbotdb::LocalDocumentId, VPackBuilder>::const_iterator
      _end;
};  // HashIndexIteratorMock

class HashIndexMock final : public darbotdb::Index {
 public:
  static std::shared_ptr<darbotdb::Index> make(
      darbotdb::IndexId iid, darbotdb::LogicalCollection& collection,
      darbotdb::velocypack::Slice const& definition) {
    auto const typeSlice = definition.get("type");

    if (typeSlice.isNone()) {
      return nullptr;
    }

    auto const type = darbotdb::basics::VelocyPackHelper::getStringView(
        typeSlice, std::string_view());

    if (type.compare("hash") != 0) {
      return nullptr;
    }

    return std::make_shared<HashIndexMock>(iid, collection, definition);
  }

  IndexType type() const override { return Index::TRI_IDX_TYPE_HASH_INDEX; }

  char const* typeName() const override { return "hash"; }

  bool canBeDropped() const override { return false; }

  bool isHidden() const override { return false; }

  bool isSorted() const override { return false; }

  bool hasSelectivityEstimate() const override { return false; }

  size_t memory() const override { return sizeof(HashIndexMock); }

  void load() override {}

  void unload() override {}

  void toVelocyPack(VPackBuilder& builder,
                    std::underlying_type<darbotdb::Index::Serialize>::type
                        flags) const override {
    builder.openObject();
    Index::toVelocyPack(builder, flags);
    builder.add("sparse", VPackValue(sparse()));
    builder.add("unique", VPackValue(unique()));
    builder.close();
  }

  void toVelocyPackFigures(VPackBuilder& builder) const override {
    Index::toVelocyPackFigures(builder);
  }

  darbotdb::Result insert(darbotdb::transaction::Methods&,
                          darbotdb::LocalDocumentId documentId,
                          darbotdb::velocypack::Slice const doc) {
    if (!doc.isObject()) {
      return {TRI_ERROR_INTERNAL};
    }

    _hashData.insert(documentId, doc);

    return {};  // ok
  }

  darbotdb::Result remove(darbotdb::transaction::Methods&,
                          darbotdb::LocalDocumentId documentId,
                          darbotdb::velocypack::Slice doc,
                          darbotdb::OperationOptions const& /*options*/) {
    if (!doc.isObject()) {
      return {TRI_ERROR_INTERNAL};
    }

    _hashData.remove(documentId, doc);

    return {};  // ok
  }

  Index::FilterCosts supportsFilterCondition(
      darbotdb::transaction::Methods& /*trx*/,
      std::vector<std::shared_ptr<darbotdb::Index>> const& allIndexes,
      darbotdb::aql::AstNode const* node,
      darbotdb::aql::Variable const* reference,
      size_t itemsInIndex) const override {
    return darbotdb::SortedIndexAttributeMatcher::supportsFilterCondition(
        allIndexes, this, node, reference, itemsInIndex);
  }

  Index::SortCosts supportsSortCondition(
      darbotdb::aql::SortCondition const* sortCondition,
      darbotdb::aql::Variable const* reference,
      size_t itemsInIndex) const override {
    return darbotdb::SortedIndexAttributeMatcher::supportsSortCondition(
        this, sortCondition, reference, itemsInIndex);
  }

  darbotdb::aql::AstNode* specializeCondition(
      darbotdb::transaction::Methods& /*trx*/, darbotdb::aql::AstNode* node,
      darbotdb::aql::Variable const* reference) const override {
    return darbotdb::SortedIndexAttributeMatcher::specializeCondition(
        this, node, reference);
  }

  std::unique_ptr<darbotdb::IndexIterator> iteratorForCondition(
      darbotdb::ResourceMonitor& monitor, darbotdb::transaction::Methods* trx,
      darbotdb::aql::AstNode const* node, darbotdb::aql::Variable const*,
      darbotdb::IndexIteratorOptions const&, darbotdb::ReadOwnWrites,
      int) override {
    darbotdb::transaction::BuilderLeaser builder(trx);
    std::unique_ptr<VPackBuilder> keys(builder.steal());
    keys->openArray();
    if (nullptr == node) {
      keys->close();
      return std::make_unique<HashIndexIteratorMock>(
          &_collection, trx, this, _hashData, std::move(keys));
    }
    TRI_ASSERT(node->type == darbotdb::aql::NODE_TYPE_OPERATOR_NARY_AND);

    std::vector<std::pair<std::vector<darbotdb::basics::AttributeName>,
                          darbotdb::aql::AstNode*>>
        allAttributes;
    for (size_t i = 0; i < node->numMembers(); ++i) {
      auto comp = node->getMember(i);
      // a.b == value
      if (!(comp->type == darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
            comp->type == darbotdb::aql::NODE_TYPE_OPERATOR_BINARY_IN)) {
        // operator type unsupported
        return std::make_unique<darbotdb::EmptyIndexIterator>(&_collection,
                                                              trx);
      }

      // assume a.b == value
      auto attrNode = comp->getMember(0);
      auto valNode = comp->getMember(1);

      if (!(attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS ||
            attrNode->type == darbotdb::aql::NODE_TYPE_EXPANSION)) {
        // got value == a.b -> flip sides
        std::swap(attrNode, valNode);
      }
      TRI_ASSERT(attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS ||
                 attrNode->type == darbotdb::aql::NODE_TYPE_EXPANSION);

      std::vector<darbotdb::basics::AttributeName> attributes;
      if (attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS) {
        do {
          attributes.emplace_back(std::string(attrNode->getStringValue(),
                                              attrNode->getStringLength()),
                                  false);
          attrNode = attrNode->getMember(0);
        } while (attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS);
        std::reverse(attributes.begin(), attributes.end());
      } else {  // expansion
        TRI_ASSERT(attrNode->type == darbotdb::aql::NODE_TYPE_EXPANSION);
        auto expNode = attrNode;
        TRI_ASSERT(expNode->numMembers() >= 2);
        auto left = expNode->getMember(0);
        TRI_ASSERT(left->type == darbotdb::aql::NODE_TYPE_ITERATOR);
        attrNode = left->getMember(1);
        TRI_ASSERT(attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS);
        do {
          attributes.emplace_back(std::string(attrNode->getStringValue(),
                                              attrNode->getStringLength()),
                                  false);
          attrNode = attrNode->getMember(0);
        } while (attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS);
        attributes.front().shouldExpand = true;
        std::reverse(attributes.begin(), attributes.end());

        std::vector<darbotdb::basics::AttributeName> attributesRight;
        attrNode = expNode->getMember(1);
        TRI_ASSERT(attrNode->type ==
                       darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS ||
                   attrNode->type == darbotdb::aql::NODE_TYPE_REFERENCE);
        while (attrNode->type == darbotdb::aql::NODE_TYPE_ATTRIBUTE_ACCESS) {
          attributesRight.emplace_back(std::string(attrNode->getStringValue(),
                                                   attrNode->getStringLength()),
                                       false);
          attrNode = attrNode->getMember(0);
        }
        attributes.insert(attributes.end(), attributesRight.crbegin(),
                          attributesRight.crend());
      }
      allAttributes.emplace_back(std::move(attributes), valNode);
    }
    size_t nullsCount = 0;
    for (auto const& f : _fields) {
      auto it =
          std::find_if(allAttributes.cbegin(), allAttributes.cend(),
                       [&f](auto const& attrs) {
                         return darbotdb::basics::AttributeName::isIdentical(
                             attrs.first, f, true);
                       });
      if (it != allAttributes.cend()) {
        while (nullsCount > 0) {
          keys->add(VPackSlice::nullSlice());
          --nullsCount;
        }
        it->second->toVelocyPackValue(*keys);
      } else {
        ++nullsCount;
      }
    }
    keys->close();

    return std::make_unique<HashIndexIteratorMock>(&_collection, trx, this,
                                                   _hashData, std::move(keys));
  }

  HashIndexMock(darbotdb::IndexId iid, darbotdb::LogicalCollection& collection,
                VPackSlice const& slice)
      : darbotdb::Index(iid, collection, slice), _hashData(_fields) {}

  /// @brief the hash table for data
  HashIndexMap _hashData;
};  // HashIndexMock

}  // namespace

PhysicalCollectionMock::DocElement::DocElement(
    std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>> data, uint64_t docId)
    : _data(data), _docId(docId) {}

darbotdb::velocypack::Slice PhysicalCollectionMock::DocElement::data() const {
  return darbotdb::velocypack::Slice(_data->data());
}

std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>
PhysicalCollectionMock::DocElement::rawData() const {
  return _data;
}

void PhysicalCollectionMock::DocElement::swapBuffer(
    std::shared_ptr<darbotdb::velocypack::Buffer<uint8_t>>& newData) {
  _data.swap(newData);
}

darbotdb::LocalDocumentId PhysicalCollectionMock::DocElement::docId() const {
  return darbotdb::LocalDocumentId::create(_docId);
}

uint8_t const* PhysicalCollectionMock::DocElement::vptr() const {
  return _data->data();
}

std::function<void()> PhysicalCollectionMock::before = []() -> void {};

PhysicalCollectionMock::PhysicalCollectionMock(
    darbotdb::LogicalCollection& collection)
    : PhysicalCollection(collection), _lastDocumentId{0} {}

darbotdb::futures::Future<std::shared_ptr<darbotdb::Index>>
PhysicalCollectionMock::createIndex(
    darbotdb::velocypack::Slice info, bool restore, bool& created,
    std::shared_ptr<std::function<darbotdb::Result(double)>> progress,
    Replication2Callback replicationCb) {
  before();

  std::vector<std::pair<darbotdb::LocalDocumentId, darbotdb::velocypack::Slice>>
      docs;
  docs.reserve(_documents.size());
  for (auto const& entry : _documents) {
    auto& doc = entry.second;
    docs.emplace_back(doc.docId(), doc.data());
  }

  struct IndexFactory : public darbotdb::IndexFactory {
    using darbotdb::IndexFactory::validateSlice;
  };
  auto id = IndexFactory::validateSlice(
      info, true, false);  // true+false to ensure id generation if missing

  auto const type = darbotdb::basics::VelocyPackHelper::getStringView(
      info.get("type"), std::string_view());

  std::shared_ptr<darbotdb::Index> index;

  if (type == "edge") {
    index = EdgeIndexMock::make(id, _logicalCollection, info);
  } else if (type == "hash") {
    index = HashIndexMock::make(id, _logicalCollection, info);
  } else if (type == "inverted") {
    index =
        StorageEngineMock::buildInvertedIndexMock(id, _logicalCollection, info);
  } else if (type == darbotdb::iresearch::StaticStrings::ViewArangoSearchType) {
    try {
      auto& server = _logicalCollection.vocbase().server();
      if (darbotdb::ServerState::instance()->isCoordinator()) {
        auto& factory =
            server.getFeature<darbotdb::iresearch::IResearchFeature>()
                .factory<darbotdb::ClusterEngine>();
        index = factory.instantiate(_logicalCollection, info, id, false);
      } else {
        index = StorageEngineMock::buildLinkMock(id, _logicalCollection, info);
      }
    } catch (std::exception const& ex) {
      // ignore the details of all errors here
      LOG_DEVEL << "caught: " << ex.what();
    }
  }

  if (!index) {
    return std::shared_ptr<darbotdb::Index>{nullptr};
  }

  TRI_vocbase_t& vocbase = _logicalCollection.vocbase();
  darbotdb::SingleCollectionTransaction trx(
      darbotdb::transaction::StandaloneContext::create(
          vocbase, darbotdb::transaction::OperationOriginTestCase{}),
      _logicalCollection, darbotdb::AccessMode::Type::WRITE);

  auto res = trx.begin();
  TRI_ASSERT(res.ok());

  if (index->type() == darbotdb::Index::TRI_IDX_TYPE_EDGE_INDEX) {
    auto* l = dynamic_cast<EdgeIndexMock*>(index.get());
    TRI_ASSERT(l != nullptr);
    for (auto const& pair : docs) {
      l->insert(trx, pair.first, pair.second);
    }
  } else if (index->type() == darbotdb::Index::TRI_IDX_TYPE_HASH_INDEX) {
    auto* l = dynamic_cast<HashIndexMock*>(index.get());
    TRI_ASSERT(l != nullptr);
    for (auto const& pair : docs) {
      l->insert(trx, pair.first, pair.second);
    }
  } else if (index->type() == darbotdb::Index::TRI_IDX_TYPE_IRESEARCH_LINK) {
    auto* l =
        dynamic_cast<darbotdb::iresearch::IResearchLinkMock*>(index.get());
    TRI_ASSERT(l != nullptr);
    for (auto const& pair : docs) {
      l->insert(trx, pair.first, pair.second);
    }
  } else if (index->type() == darbotdb::Index::TRI_IDX_TYPE_INVERTED_INDEX) {
    auto* l = dynamic_cast<darbotdb::iresearch::IResearchInvertedIndexMock*>(
        index.get());
    TRI_ASSERT(l != nullptr);
    for (auto const& pair : docs) {
      l->insert(trx, pair.first, pair.second);
    }
  } else {
    TRI_ASSERT(false);
  }

  _indexes.emplace(index);
  created = true;

  res = trx.commit();
  TRI_ASSERT(res.ok());

  if (index->type() == darbotdb::Index::TRI_IDX_TYPE_INVERTED_INDEX) {
    auto* l = dynamic_cast<darbotdb::iresearch::IResearchInvertedIndexMock*>(
        index.get());
    TRI_ASSERT(l != nullptr);
    auto commitRes = l->commit();
    TRI_ASSERT(commitRes.ok());
  }

  return index;
}

void PhysicalCollectionMock::deferDropCollection(
    std::function<bool(darbotdb::LogicalCollection&)> const& callback) {
  before();

  callback(_logicalCollection);  // assume noone is using this collection (drop
                                 // immediately)
}

darbotdb::Result PhysicalCollectionMock::dropIndex(darbotdb::IndexId iid) {
  before();

  for (auto itr = _indexes.begin(), end = _indexes.end(); itr != end; ++itr) {
    if ((*itr)->id() == iid) {
      if ((*itr)->drop().ok()) {
        _indexes.erase(itr);
        return {};
      }
    }
  }

  return {TRI_ERROR_INTERNAL};
}

void PhysicalCollectionMock::figuresSpecific(bool /*details*/,
                                             darbotdb::velocypack::Builder&) {
  before();
  TRI_ASSERT(false);
}

std::unique_ptr<darbotdb::IndexIterator> PhysicalCollectionMock::getAllIterator(
    darbotdb::transaction::Methods* trx,
    darbotdb::ReadOwnWrites readOwnWrites) const {
  before();

  return std::make_unique<AllIteratorMock>(_documents, this->_logicalCollection,
                                           trx, readOwnWrites);
}

std::unique_ptr<darbotdb::IndexIterator> PhysicalCollectionMock::getAnyIterator(
    darbotdb::transaction::Methods* trx) const {
  before();
  return std::make_unique<AllIteratorMock>(_documents, this->_logicalCollection,
                                           trx, darbotdb::ReadOwnWrites::no);
}

std::unique_ptr<darbotdb::ReplicationIterator>
PhysicalCollectionMock::getReplicationIterator(
    darbotdb::ReplicationIterator::Ordering, uint64_t) {
  return nullptr;
}

void PhysicalCollectionMock::getPropertiesVPack(
    darbotdb::velocypack::Builder&) const {
  before();
}

darbotdb::Result PhysicalCollectionMock::insert(
    darbotdb::transaction::Methods& trx,
    darbotdb::IndexesSnapshot const& /*indexesSnapshot*/,
    darbotdb::RevisionId newRevisionId, darbotdb::velocypack::Slice newDocument,
    darbotdb::OperationOptions const& options) {
  before();

  TRI_ASSERT(newDocument.isObject());
  TRI_ASSERT(newDocument.get(darbotdb::StaticStrings::KeyString).isString());
  VPackSlice newKey = newDocument.get(darbotdb::StaticStrings::KeyString);
  if (newKey.isString() && _documents.contains(newKey.stringView())) {
    return {TRI_ERROR_DDB_UNIQUE_CONSTRAINT_VIOLATED};
  }

  std::string_view key{newKey.stringView()};

  auto buffer = std::make_shared<darbotdb::velocypack::Buffer<uint8_t>>();
  buffer->append(newDocument.start(), newDocument.byteSize());
  // key is a string_view, and it must point into the storage space that
  // we own and that keeps valid
  key = darbotdb::velocypack::Slice(buffer->data())
            .get(darbotdb::StaticStrings::KeyString)
            .stringView();

  darbotdb::LocalDocumentId id =
      ::generateDocumentId(_logicalCollection, newRevisionId, _lastDocumentId);
  auto const& [ref, didInsert] =
      _documents.emplace(key, DocElement{std::move(buffer), id.id()});
  TRI_ASSERT(didInsert);

  for (auto& index : _indexes) {
    if (index->type() == darbotdb::Index::TRI_IDX_TYPE_EDGE_INDEX) {
      auto* l = static_cast<EdgeIndexMock*>(index.get());
      if (!l->insert(trx, id, newDocument).ok()) {
        return {TRI_ERROR_BAD_PARAMETER};
      }
      continue;
    } else if (index->type() == darbotdb::Index::TRI_IDX_TYPE_HASH_INDEX) {
      auto* l = static_cast<HashIndexMock*>(index.get());
      if (!l->insert(trx, id, newDocument).ok()) {
        return {TRI_ERROR_BAD_PARAMETER};
      }
      continue;
    } else if (index->type() == darbotdb::Index::TRI_IDX_TYPE_IRESEARCH_LINK) {
      auto* l =
          static_cast<darbotdb::iresearch::IResearchLinkMock*>(index.get());
      if (!l->insert(trx, id, newDocument).ok()) {
        return {TRI_ERROR_BAD_PARAMETER};
      }
      continue;
    } else if (index->type() == darbotdb::Index::TRI_IDX_TYPE_INVERTED_INDEX) {
      auto* l = static_cast<darbotdb::iresearch::IResearchInvertedIndexMock*>(
          index.get());
      if (!l->insert(trx, ref->second.docId(), newDocument).ok()) {
        return {TRI_ERROR_BAD_PARAMETER};
      }
      continue;
    }
    TRI_ASSERT(false);
  }
  auto* state = darbotdb::basics::downCast<TransactionStateMock>(trx.state());
  TRI_ASSERT(state != nullptr);
  state->incrementInsert();

  return {};
}

darbotdb::Result PhysicalCollectionMock::lookupKey(
    darbotdb::transaction::Methods*, std::string_view key,
    std::pair<darbotdb::LocalDocumentId, darbotdb::RevisionId>& result,
    darbotdb::ReadOwnWrites) const {
  before();

  auto it = _documents.find(key);
  if (it != _documents.end()) {
    result.first = it->second.docId();
    result.second = darbotdb::RevisionId::fromSlice(it->second.data());
    return {};
  }

  result.first = darbotdb::LocalDocumentId::none();
  result.second = darbotdb::RevisionId::none();
  return {TRI_ERROR_DDB_DOCUMENT_NOT_FOUND};
}

darbotdb::Result PhysicalCollectionMock::lookupKeyForUpdate(
    darbotdb::transaction::Methods* methods, std::string_view key,
    std::pair<darbotdb::LocalDocumentId, darbotdb::RevisionId>& result) const {
  return lookupKey(methods, key, result, darbotdb::ReadOwnWrites::yes);
}

uint64_t PhysicalCollectionMock::numberDocuments(
    darbotdb::transaction::Methods*) const {
  before();
  return _documents.size();
}

bool PhysicalCollectionMock::addIndex(std::shared_ptr<darbotdb::Index> idx) {
  auto const id = idx->id();
  for (auto const& it : _indexes) {
    if (it->id() == id) {
      // already have this particular index. do not add it again
      return false;
    }
  }

  TRI_UpdateTickServer(static_cast<TRI_voc_tick_t>(id.id()));

  _indexes.emplace(idx);
  return true;
}

void PhysicalCollectionMock::prepareIndexes(
    darbotdb::velocypack::Slice indexesSlice) {
  before();

  auto& engine = _logicalCollection.vocbase()
                     .server()
                     .getFeature<darbotdb::EngineSelectorFeature>()
                     .engine();
  auto& idxFactory = engine.indexFactory();

  for (VPackSlice v : VPackArrayIterator(indexesSlice)) {
    if (darbotdb::basics::VelocyPackHelper::getBooleanValue(v, "error",
                                                            false)) {
      // We have an error here.
      // Do not add index.
      continue;
    }

    try {
      auto idx =
          idxFactory.prepareIndexFromSlice(v, false, _logicalCollection, true);

      if (!idx) {
        continue;
      }

      if (!addIndex(idx)) {
        return;
      }
    } catch (std::exception const&) {
      // error is just ignored here
    }
  }
}

darbotdb::IndexEstMap PhysicalCollectionMock::clusterIndexEstimates(
    bool allowUpdating, darbotdb::TransactionId tid) {
  TRI_ASSERT(darbotdb::ServerState::instance()->isCoordinator());
  darbotdb::IndexEstMap estimates;
  for (auto const& it : _indexes) {
    std::string id = std::to_string(it->id().id());
    if (it->hasSelectivityEstimate()) {
      // Note: This may actually be bad, as this instance cannot
      // have documents => The estimate is off.
      estimates.emplace(std::move(id), it->selectivityEstimate());
    } else {
      // Random hardcoded estimate. We do not actually know anything
      estimates.emplace(std::move(id), 0.25);
    }
  }
  return estimates;
}
darbotdb::Result PhysicalCollectionMock::lookup(
    darbotdb::transaction::Methods* trx, std::string_view key,
    darbotdb::IndexIterator::DocumentCallback const& cb,
    LookupOptions options) const {
  before();
  auto it = _documents.find(key);
  if (it != _documents.end()) {
    cb(it->second.docId(), nullptr,
       darbotdb::velocypack::Slice(it->second.vptr()));
    return darbotdb::Result(TRI_ERROR_NO_ERROR);
  }
  return darbotdb::Result(TRI_ERROR_DDB_DOCUMENT_NOT_FOUND);
}
darbotdb::Result PhysicalCollectionMock::lookup(
    darbotdb::transaction::Methods* trx, darbotdb::LocalDocumentId token,
    darbotdb::IndexIterator::DocumentCallback const& cb, LookupOptions options,
    darbotdb::StorageSnapshot const* snapshot) const {
  before();
  for (auto const& entry : _documents) {
    auto& doc = entry.second;
    if (doc.docId() == token) {
      cb(token, nullptr, doc.data());
      return darbotdb::Result{};
    }
  }
  return darbotdb::Result{TRI_ERROR_DDB_DOCUMENT_NOT_FOUND};
}

darbotdb::Result PhysicalCollectionMock::lookup(
    darbotdb::transaction::Methods* trx,
    std::span<darbotdb::LocalDocumentId> tokens,
    MultiDocumentCallback const& cb, LookupOptions options) const {
  before();
  for (auto token : tokens) {
    bool found = false;
    for (auto const& entry : _documents) {
      auto& doc = entry.second;
      if (doc.docId() == token) {
        cb(darbotdb::Result{}, token, nullptr, doc.data());
        found = true;
        break;
      }
    }
    if (!found) {
      cb(darbotdb::Result{TRI_ERROR_DDB_DOCUMENT_NOT_FOUND}, token, nullptr,
         {});
    }
  }
  return darbotdb::Result{};
}

darbotdb::Result PhysicalCollectionMock::remove(
    darbotdb::transaction::Methods& trx,
    darbotdb::IndexesSnapshot const& /*indexesSnapshot*/,
    darbotdb::LocalDocumentId previousDocumentId,
    darbotdb::RevisionId previousRevisionId,
    darbotdb::velocypack::Slice previousDocument,
    darbotdb::OperationOptions const& options) {
  before();

  std::string_view key;
  if (previousDocument.isString()) {
    key = previousDocument.stringView();
  } else {
    key = previousDocument.get(darbotdb::StaticStrings::KeyString).stringView();
  }
  auto old = _documents.find(key);
  if (old != _documents.end()) {
    TRI_ASSERT(previousRevisionId ==
               darbotdb::RevisionId::fromSlice(old->second.data()));
    _documents.erase(old);
    // TODO: removing the document from the mock collection
    // does not remove it from any mock indexes

    // assume document was removed
    auto* state = darbotdb::basics::downCast<TransactionStateMock>(trx.state());
    TRI_ASSERT(state != nullptr);
    state->incrementRemove();
    return {};
  }
  return {TRI_ERROR_DDB_DOCUMENT_NOT_FOUND};
}

darbotdb::Result PhysicalCollectionMock::update(
    darbotdb::transaction::Methods& trx,
    darbotdb::IndexesSnapshot const& /*indexesSnapshot*/,
    darbotdb::LocalDocumentId newDocumentId,
    darbotdb::RevisionId previousRevisionId,
    darbotdb::velocypack::Slice previousDocument,
    darbotdb::RevisionId newRevisionId, darbotdb::velocypack::Slice newDocument,
    darbotdb::OperationOptions const& options) {
  return updateInternal(trx, newDocumentId, previousRevisionId,
                        previousDocument, newRevisionId, newDocument, options,
                        /*isUpdate*/ true);
}

darbotdb::Result PhysicalCollectionMock::replace(
    darbotdb::transaction::Methods& trx,
    darbotdb::IndexesSnapshot const& /*indexesSnapshot*/,
    darbotdb::LocalDocumentId newDocumentId,
    darbotdb::RevisionId previousRevisionId,
    darbotdb::velocypack::Slice previousDocument,
    darbotdb::RevisionId newRevisionId, darbotdb::velocypack::Slice newDocument,
    darbotdb::OperationOptions const& options) {
  return updateInternal(trx, newDocumentId, previousRevisionId,
                        previousDocument, newRevisionId, newDocument, options,
                        /*isUpdate*/ false);
}

darbotdb::RevisionId PhysicalCollectionMock::revision(
    darbotdb::transaction::Methods*) const {
  before();
  TRI_ASSERT(false);
  return darbotdb::RevisionId::none();
}

darbotdb::Result PhysicalCollectionMock::truncate(
    darbotdb::transaction::Methods& trx, darbotdb::OperationOptions& options,
    bool& usedRangeDelete) {
  before();
  _documents.clear();

  // should not matter what we set here
  usedRangeDelete = true;
  return {};
}

darbotdb::Result PhysicalCollectionMock::updateInternal(
    darbotdb::transaction::Methods& trx,
    darbotdb::LocalDocumentId /*newDocumentId*/,
    darbotdb::RevisionId previousRevisionId,
    darbotdb::velocypack::Slice /*previousDocument*/,
    darbotdb::RevisionId /*newRevisionId*/,
    darbotdb::velocypack::Slice newDocument,
    darbotdb::OperationOptions const& options, bool isUpdate) {
  TRI_ASSERT(newDocument.isObject());
  auto keySlice = newDocument.get(darbotdb::StaticStrings::KeyString);
  if (!keySlice.isString()) {
    return {TRI_ERROR_DDB_DOCUMENT_HANDLE_BAD};
  }

  before();
  std::string_view key{keySlice.stringView()};
  auto it = _documents.find(key);
  if (it != _documents.end()) {
    auto doc = it->second.data();
    TRI_ASSERT(doc.isObject());

    // replace document
    auto newBuffer = std::make_shared<darbotdb::velocypack::Buffer<uint8_t>>();
    newBuffer->append(newDocument.start(), newDocument.byteSize());
    // key is a string_view, and it must point into the storage space that
    // we own and that keeps valid
    key = darbotdb::velocypack::Slice(newBuffer->data())
              .get(darbotdb::StaticStrings::KeyString)
              .stringView();

    auto docId = it->second.docId();
    // must remove and insert, because our map's key type is a string_view.
    // the string_view could point to invalid memory if we change a map
    // entry's contents.
    _documents.erase(it);
    auto const& [ref, didInsert] =
        _documents.emplace(key, DocElement{std::move(newBuffer), docId.id()});
    TRI_ASSERT(didInsert);

    auto* state = darbotdb::basics::downCast<TransactionStateMock>(trx.state());
    TRI_ASSERT(state != nullptr);
    state->incrementRemove();
    state->incrementInsert();

    // TODO: mock index entries are not updated here
    return {};
  }
  return {TRI_ERROR_DDB_DOCUMENT_NOT_FOUND};
}

darbotdb::Result PhysicalCollectionMock::updateProperties(
    darbotdb::velocypack::Slice slice) {
  before();

  return darbotdb::Result(
      TRI_ERROR_NO_ERROR);  // assume mock collection updated OK
}
