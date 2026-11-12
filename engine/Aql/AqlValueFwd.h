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

#include <functional>

namespace darbotdb {
namespace aql {

struct AqlValue;

}  // namespace aql
}  // namespace darbotdb

/// @brief hash function for AqlValue objects
/// Defined in AqlValue.cpp!
namespace std {

template<>
struct hash<darbotdb::aql::AqlValue> {
  size_t operator()(darbotdb::aql::AqlValue const& x) const noexcept;
};

template<>
struct equal_to<darbotdb::aql::AqlValue> {
  bool operator()(darbotdb::aql::AqlValue const& a,
                  darbotdb::aql::AqlValue const& b) const noexcept;
};

}  // namespace std
