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

#include "Aql/AqlValue.h"
#include "Basics/ErrorCode.h"
#include "Containers/SmallVector.h"

#include <span>
#include <string_view>

namespace darbotdb {
class Result;

namespace transaction {
class Methods;
}

namespace velocypack {
class Slice;
}

namespace aql {
struct AstNode;
class ExpressionContext;

namespace functions {

using VPackFunctionParameters = containers::SmallVector<AqlValue, 4>;
using VPackFunctionParametersView = std::span<AqlValue const>;

typedef AqlValue (*FunctionImplementation)(darbotdb::aql::ExpressionContext*,
                                           AstNode const&,
                                           VPackFunctionParametersView);

void registerError(ExpressionContext* expressionContext,
                   std::string_view functionName, ErrorCode code);
void registerWarning(ExpressionContext* expressionContext,
                     std::string_view functionName, ErrorCode code);
void registerWarning(ExpressionContext* expressionContext,
                     std::string_view functionName, Result const& rr);
void registerInvalidArgumentWarning(ExpressionContext* expressionContext,
                                    std::string_view functionName);

// Returns zero-terminated function name from the given FCALL node.
std::string_view getFunctionName(AstNode const& node) noexcept;

bool getBooleanParameter(VPackFunctionParametersView parameters,
                         size_t startParameter, bool defaultValue);

AqlValue const& extractFunctionParameterValue(
    VPackFunctionParametersView parameters, size_t position);

AqlValue numberValue(double value, bool nullify);

std::string extractCollectionName(transaction::Methods* trx,
                                  VPackFunctionParametersView parameters,
                                  size_t position);

template<typename T>
void appendAsString(velocypack::Options const& vopts, T& buffer,
                    AqlValue const& value);

/// @brief helper function. not callable as a "normal" AQL function
template<typename T>
void stringify(velocypack::Options const* vopts, T& buffer,
               darbotdb::velocypack::Slice slice);

AqlValue IsNull(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue IsBool(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue IsNumber(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue IsString(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue IsArray(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue IsObject(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue Typename(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue ToNumber(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue ToString(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue ToBool(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue ToArray(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Length(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue FindFirst(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue FindLast(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue Reverse(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue First(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Last(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Nth(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Contains(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue Concat(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue ConcatSeparator(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue CharLength(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue Lower(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Upper(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Substring(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue SubstringBytes(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue Substitute(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue Left(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Right(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Trim(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue LTrim(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue RTrim(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Split(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Like(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue RegexMatches(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue RegexTest(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue RegexReplace(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue RegexSplit(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue ToBase64(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue ToHex(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue ToChar(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Repeat(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue EncodeURIComponent(darbotdb::aql::ExpressionContext*, AstNode const&,
                            VPackFunctionParametersView);
AqlValue Uuid(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Soundex(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue LevenshteinDistance(darbotdb::aql::ExpressionContext*, AstNode const&,
                             VPackFunctionParametersView);
AqlValue LevenshteinMatch(darbotdb::aql::ExpressionContext*, AstNode const&,
                          VPackFunctionParametersView);
AqlValue NgramSimilarity(ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue NgramPositionalSimilarity(ExpressionContext* ctx, AstNode const&,
                                   VPackFunctionParametersView);
AqlValue NgramMatch(ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue InRange(ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
// Date
AqlValue DateNow(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue DateIso8601(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue DateTimestamp(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
/**
 * @brief Tests is the first given parameter is a valid date string
 * format.
 *
 * @param query The AQL Query
 * @param trx The ongoing transaction
 * @param params List of input parameters
 *
 * @return AQLValue(true) iff the first input parameter is a string,r
 *         and is of valid date format. (May return true on invalid dates
 *         like 2018-02-31)
 */
AqlValue IsDatestring(darbotdb::aql::ExpressionContext* query, AstNode const&,
                      VPackFunctionParametersView params);
AqlValue DateDayOfWeek(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue DateYear(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue DateMonth(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue DateDay(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue DateHour(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue DateMinute(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue DateSecond(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue DateMillisecond(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue DateDayOfYear(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue DateIsoWeek(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue DateIsoWeekYear(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue DateLeapYear(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue DateQuarter(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue DateDaysInMonth(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue DateTrunc(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue DateUtcToLocal(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue DateLocalToUtc(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue DateTimeZone(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue DateTimeZones(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue DateAdd(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue DateSubtract(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue DateDiff(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue DateRound(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
/**
 * @brief Compares two dates given as the first two arguments.
 *        Third argument defines the highest signficant part,
 *        Forth (optional) defines the lowest significant part.
 *
 * @param query The AQL query
 * @param trx The ongoing transaction
 * @param params 3 or 4 Parameters.
 *         1. A date string
 *         2. Another date string.
 *         3. Modifier year/day/houer etc. Will compare 1 to 2
 *            based on this value.
 *         4. Another modifier (like 3), optional. If given
 *            will compare all parts starting from modifier
 *            in (3) to this modifier.
 *
 * Example:
 * (DateA, DateB, month, minute)
 * Will compare A to B considering month, day, hour and minute.
 * It will ignore year, second and millisecond.
 *
 * @return AQLValue(TRUE) if the dates are equal. AQLValue(FALSE)
 * if they differ. May return AQLValue(NULL) on invalid input
 */
AqlValue DateCompare(darbotdb::aql::ExpressionContext* query, AstNode const&,
                     VPackFunctionParametersView params);

AqlValue DateFormat(darbotdb::aql::ExpressionContext* query, AstNode const&,
                    VPackFunctionParametersView params);
AqlValue Passthru(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue Unset(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue UnsetRecursive(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue Keep(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue KeepRecursive(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue Translate(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue Merge(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue MergeRecursive(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue Has(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Attributes(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue Values(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Value(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Min(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Max(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Sum(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Average(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Product(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Sleep(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Collections(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue RandomToken(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue IpV4FromNumber(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue IpV4ToNumber(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue IsIpV4(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Md5(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Sha1(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Sha256(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Sha512(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Crc32(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Fnv64(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Hash(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue IsKey(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue CountDistinct(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue CheckDocument(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue Unique(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue SortedUnique(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue Sorted(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Union(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue UnionDistinct(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue Intersection(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue Outersection(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue Jaccard(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Distance(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue GeoInRange(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue GeoDistance(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue GeoContains(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue GeoIntersects(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue GeoEquals(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue GeoArea(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue IsInPolygon(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue GeoPoint(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue GeoMultiPoint(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue GeoPolygon(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue GeoMultiPolygon(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue GeoLinestring(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue GeoMultiLinestring(darbotdb::aql::ExpressionContext*, AstNode const&,
                            VPackFunctionParametersView);
AqlValue Flatten(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Zip(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Entries(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue JsonStringify(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue JsonParse(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue ParseIdentifier(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue ParseKey(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue ParseCollection(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue Slice(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Minus(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Document(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue Matches(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Round(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Abs(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Ceil(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Floor(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Sqrt(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Pow(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Log(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Log2(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Log10(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Exp(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Exp2(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Sin(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Cos(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Tan(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Asin(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Acos(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Atan(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Atan2(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Radians(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Degrees(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Pi(darbotdb::aql::ExpressionContext*, AstNode const&,
            VPackFunctionParametersView);
AqlValue BitAnd(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue BitOr(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue BitXOr(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue BitNegate(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue BitTest(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue BitPopcount(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue BitShiftLeft(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue BitShiftRight(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue BitConstruct(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue BitDeconstruct(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue BitFromString(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue BitToString(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue Rand(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue FirstDocument(darbotdb::aql::ExpressionContext*, AstNode const&,
                       VPackFunctionParametersView);
AqlValue FirstList(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue Push(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Pop(darbotdb::aql::ExpressionContext*, AstNode const&,
             VPackFunctionParametersView);
AqlValue Append(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Unshift(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue Shift(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue RemoveValue(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);
AqlValue RemoveValues(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue RemoveNth(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue ReplaceNth(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue Interleave(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue NotNull(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue CurrentDatabase(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue CollectionCount(darbotdb::aql::ExpressionContext*, AstNode const&,
                         VPackFunctionParametersView);
AqlValue VarianceSample(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);
AqlValue VariancePopulation(darbotdb::aql::ExpressionContext*, AstNode const&,
                            VPackFunctionParametersView);
AqlValue StdDevSample(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);
AqlValue StdDevPopulation(darbotdb::aql::ExpressionContext*, AstNode const&,
                          VPackFunctionParametersView);
AqlValue Median(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Percentile(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);
AqlValue Range(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Position(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);
AqlValue Call(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Apply(darbotdb::aql::ExpressionContext*, AstNode const&,
               VPackFunctionParametersView);
AqlValue Version(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue IsSameCollection(darbotdb::aql::ExpressionContext*, AstNode const&,
                          VPackFunctionParametersView);
AqlValue Assert(darbotdb::aql::ExpressionContext*, AstNode const&,
                VPackFunctionParametersView);
AqlValue Warn(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue Fail(darbotdb::aql::ExpressionContext*, AstNode const&,
              VPackFunctionParametersView);
AqlValue DecodeRev(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue ShardId(darbotdb::aql::ExpressionContext*, AstNode const&,
                 VPackFunctionParametersView);
AqlValue CurrentUser(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);

AqlValue SchemaGet(darbotdb::aql::ExpressionContext*, AstNode const&,
                   VPackFunctionParametersView);
AqlValue SchemaValidate(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);

AqlValue MakeDistributeInput(darbotdb::aql::ExpressionContext*, AstNode const&,
                             VPackFunctionParametersView);
AqlValue MakeDistributeInputWithKeyCreation(darbotdb::aql::ExpressionContext*,
                                            AstNode const&,
                                            VPackFunctionParametersView);
AqlValue MakeDistributeGraphInput(darbotdb::aql::ExpressionContext*,
                                  AstNode const&, VPackFunctionParametersView);

#ifdef USE_ENTERPRISE
AqlValue SelectSmartDistributeGraphInput(darbotdb::aql::ExpressionContext*,
                                         AstNode const&,
                                         VPackFunctionParametersView);
#endif

AqlValue ApproxNearCosine(darbotdb::aql::ExpressionContext*, AstNode const&,
                          VPackFunctionParametersView);

AqlValue ApproxNearL2(darbotdb::aql::ExpressionContext*, AstNode const&,
                      VPackFunctionParametersView);

AqlValue ApproxNearInnerProduct(darbotdb::aql::ExpressionContext*,
                                AstNode const&, VPackFunctionParametersView);

AqlValue DecayGauss(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);

AqlValue DecayExp(darbotdb::aql::ExpressionContext*, AstNode const&,
                  VPackFunctionParametersView);

AqlValue DecayLinear(darbotdb::aql::ExpressionContext*, AstNode const&,
                     VPackFunctionParametersView);

AqlValue CosineSimilarity(darbotdb::aql::ExpressionContext*, AstNode const&,
                          VPackFunctionParametersView);

AqlValue L1Distance(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);

AqlValue L2Distance(darbotdb::aql::ExpressionContext*, AstNode const&,
                    VPackFunctionParametersView);

/// @brief dummy function that will only throw an error when called
AqlValue NotImplemented(darbotdb::aql::ExpressionContext*, AstNode const&,
                        VPackFunctionParametersView);

aql::AqlValue NotImplementedEE(aql::ExpressionContext*, aql::AstNode const&,
                               std::span<aql::AqlValue const>);

aql::AqlValue MinHash(aql::ExpressionContext*, aql::AstNode const&,
                      std::span<aql::AqlValue const>);

aql::AqlValue MinHashError(aql::ExpressionContext*, aql::AstNode const&,
                           std::span<aql::AqlValue const>);

aql::AqlValue MinHashCount(aql::ExpressionContext*, aql::AstNode const&,
                           std::span<aql::AqlValue const>);

aql::AqlValue MinHashMatch(aql::ExpressionContext*, aql::AstNode const&,
                           std::span<aql::AqlValue const>);

/// @brief maximum precision for bit operations
constexpr uint64_t bitFunctionsMaxSupportedBits = 32;
constexpr uint64_t bitFunctionsMaxSupportedValue =
    ((uint64_t(1) << bitFunctionsMaxSupportedBits) - uint64_t(1));

}  // namespace functions
}  // namespace aql
}  // namespace darbotdb
