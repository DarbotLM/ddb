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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Basics/ResourceUsage.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace darbotdb::aql {

/// @brief helper class to handle top level and nested attribute paths.
/// a top-level attribute (e.g. _key) will be stored in a vector with a single
/// element. nested attributes (e.g. a.b.c) will be stored in a vector with
/// multiple elements
class AttributeNamePath {
 public:
  enum class Type : uint8_t {
    IdAttribute,      // _id
    KeyAttribute,     // _key
    FromAttribute,    // _from
    ToAttribute,      // _to
    SingleAttribute,  // any other top-level attribute
    MultiAttribute    // sub-attribute, e.g. a.b.c
  };

  explicit AttributeNamePath(darbotdb::ResourceMonitor& resourceMonitor);

  /// @brief construct an attribute path from a single attribute (e.g. _key)
  AttributeNamePath(std::string attribute,
                    darbotdb::ResourceMonitor& resourceMonitor);

  /// @brief construct an attribute path from a nested attribute (e.g. a.b.c)
  AttributeNamePath(std::vector<std::string> path,
                    darbotdb::ResourceMonitor& resourceMonitor);

  ~AttributeNamePath();

  AttributeNamePath(AttributeNamePath const& other);
  AttributeNamePath& operator=(AttributeNamePath const& other);
  AttributeNamePath(AttributeNamePath&& other) noexcept;
  AttributeNamePath& operator=(AttributeNamePath&& other) noexcept;

  /// @brief if the path is empty
  bool empty() const noexcept;

  /// @brief get the number of attributes in the path
  size_t size() const noexcept;

  /// @brief get the attribute path type
  Type type() const noexcept;

  /// @brief hash the path
  size_t hash() const noexcept;

  /// @brief get attribute at level
  std::string_view operator[](size_t index) const noexcept;

  bool operator==(AttributeNamePath const& other) const noexcept;
  bool operator!=(AttributeNamePath const& other) const noexcept {
    return !operator==(other);
  }

  bool operator<(AttributeNamePath const& other) const noexcept;

  /// @brief get the full path
  [[nodiscard]] std::vector<std::string> const& get() const noexcept;

  /// @brief clear all path attributes
  void clear() noexcept;

  /// @brief add an attribute to the path
  void add(std::string part);

#ifdef darbotdb_USE_GOOGLE_TESTS
  void pop();
#endif

  bool isPrefixOf(AttributeNamePath const& other) const noexcept;

  /// @brief reverse the attributes in the path
  AttributeNamePath& reverse();

  /// @brief shorten the attributes in the path to the specified length
  AttributeNamePath& shortenTo(size_t length);

  /// @brief determines the length of common prefixes
  static size_t commonPrefixLength(AttributeNamePath const& lhs,
                                   AttributeNamePath const& rhs);

 private:
  size_t memoryUsage() const noexcept;

  darbotdb::ResourceMonitor& _resourceMonitor;
  std::vector<std::string> _path;
};

std::ostream& operator<<(std::ostream& stream, AttributeNamePath const& path);

}  // namespace darbotdb::aql

namespace std {

template<>
struct hash<darbotdb::aql::AttributeNamePath> {
  size_t operator()(
      darbotdb::aql::AttributeNamePath const& value) const noexcept {
    return value.hash();
  }
};

template<>
struct equal_to<darbotdb::aql::AttributeNamePath> {
  bool operator()(darbotdb::aql::AttributeNamePath const& lhs,
                  darbotdb::aql::AttributeNamePath const& rhs) const noexcept {
    return lhs == rhs;
  }
};

}  // namespace std
