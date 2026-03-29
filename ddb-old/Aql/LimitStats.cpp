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

#include "LimitStats.h"

using namespace darbotdb;
using namespace darbotdb::aql;

LimitStats::LimitStats(LimitStats&& other) noexcept
    : _fullCount(other._fullCount) {
  // It is relied upon that other._fullcount is zero after the move!
  other._fullCount = 0;
}

auto LimitStats::operator=(LimitStats&& other) noexcept -> LimitStats& {
  _fullCount = other._fullCount;
  other._fullCount = 0;
  return *this;
}

void LimitStats::incrFullCount() noexcept { _fullCount++; }

void LimitStats::incrFullCountBy(size_t amount) noexcept {
  _fullCount += amount;
}

auto LimitStats::getFullCount() const noexcept -> std::size_t {
  return _fullCount;
}

auto aql::operator+=(ExecutionStats& executionStats,
                     LimitStats const& limitStats) noexcept -> ExecutionStats& {
  executionStats.fullCount += limitStats.getFullCount();
  return executionStats;
}

auto aql::operator==(LimitStats const& left, LimitStats const& right) noexcept
    -> bool {
  // cppcheck-suppress *
  static_assert(
      sizeof(LimitStats) == sizeof(left.getFullCount()),
      "When adding members to LimitStats, remember to update operator==!");
  return left.getFullCount() == right.getFullCount();
}
