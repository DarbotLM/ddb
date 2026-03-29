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
/// @author Andrei Lobov
////////////////////////////////////////////////////////////////////////////////

#include "gtest/gtest.h"
#include "Basics/StaticStrings.h"
#include "Cluster/ClusterTypes.h"
#include "velocypack/Slice.h"
#include "velocypack/Parser.h"
#include <velocypack/Builder.h>
#include <ostream>

TEST(QueryAnalyzerRevisionsTest, fullData) {
  darbotdb::QueryAnalyzerRevisions revisions;
  ASSERT_TRUE(revisions.isDefault());
  ASSERT_TRUE(revisions
                  .fromVelocyPack(VPackParser::fromJson(
                                      "{\"analyzersRevision\" : { \"current\" "
                                      ": 10, \"system\": 11}}")
                                      ->slice())
                  .ok());
  ASSERT_EQ(darbotdb::QueryAnalyzerRevisions(10, 11), revisions);
}

TEST(QueryAnalyzerRevisionsTest, emptyData) {
  darbotdb::QueryAnalyzerRevisions revisions;
  ASSERT_TRUE(
      revisions
          .fromVelocyPack(
              VPackParser::fromJson("{\"analyzersRevision\" : {}}")->slice())
          .ok());
  ASSERT_EQ(darbotdb::QueryAnalyzerRevisions(darbotdb::AnalyzersRevision::MIN,
                                             darbotdb::AnalyzersRevision::MIN),
            revisions);
}

TEST(QueryAnalyzerRevisionsTest, noData) {
  darbotdb::QueryAnalyzerRevisions revisions(20, 30);
  ASSERT_TRUE(revisions
                  .fromVelocyPack(VPackParser::fromJson(
                                      "{\"SomeOtherParameter\" : { \"current\" "
                                      ": 10, \"system\": 11}}")
                                      ->slice())
                  .ok());
  ASSERT_EQ(darbotdb::QueryAnalyzerRevisions(), revisions);
}

TEST(QueryAnalyzerRevisionsTest, onlySystemData) {
  darbotdb::QueryAnalyzerRevisions revisions(20, 30);
  ASSERT_FALSE(revisions.isDefault());
  ASSERT_TRUE(
      revisions
          .fromVelocyPack(VPackParser::fromJson(
                              "{\"analyzersRevision\" : {\"system\": 11}}")
                              ->slice())
          .ok());
  ASSERT_EQ(
      darbotdb::QueryAnalyzerRevisions(darbotdb::AnalyzersRevision::MIN, 11),
      revisions);
}

TEST(QueryAnalyzerRevisionsTest, onlyCurrentData) {
  darbotdb::QueryAnalyzerRevisions revisions(20, 30);
  ASSERT_FALSE(revisions.isDefault());
  ASSERT_TRUE(
      revisions
          .fromVelocyPack(VPackParser::fromJson(
                              "{\"analyzersRevision\" : {\"current\": 11}}")
                              ->slice())
          .ok());
  ASSERT_EQ(
      darbotdb::QueryAnalyzerRevisions(11, darbotdb::AnalyzersRevision::MIN),
      revisions);
}

TEST(QueryAnalyzerRevisionsTest, invalidCurrent) {
  darbotdb::QueryAnalyzerRevisions revisions;
  ASSERT_TRUE(revisions
                  .fromVelocyPack(VPackParser::fromJson(
                                      "{\"analyzersRevision\" : {\"current\": "
                                      "\"GG\", \"system\": 10}}")
                                      ->slice())
                  .fail());
}

TEST(QueryAnalyzerRevisionsTest, invalidSystem) {
  darbotdb::QueryAnalyzerRevisions revisions;
  ASSERT_TRUE(revisions
                  .fromVelocyPack(VPackParser::fromJson(
                                      "{\"analyzersRevision\" : {\"system\": "
                                      "\"GG\", \"current\": 10}}")
                                      ->slice())
                  .fail());
}

TEST(QueryAnalyzerRevisionsTest, getVocbaseRevision) {
  darbotdb::QueryAnalyzerRevisions revisions(1, 2);
  ASSERT_EQ(1, revisions.getVocbaseRevision("my_database"));
  ASSERT_EQ(
      2, revisions.getVocbaseRevision(darbotdb::StaticStrings::SystemDatabase));
}

TEST(QueryAnalyzerRevisionsTest, equality) {
  darbotdb::QueryAnalyzerRevisions revisions12(1, 2);
  darbotdb::QueryAnalyzerRevisions revisions32(3, 2);
  darbotdb::QueryAnalyzerRevisions revisions13(1, 3);
  darbotdb::QueryAnalyzerRevisions revisions12_2(1, 2);
  ASSERT_TRUE(revisions12 == revisions12);
  ASSERT_FALSE(revisions12 != revisions12);
  ASSERT_TRUE(revisions12_2 == revisions12);
  ASSERT_FALSE(revisions12_2 != revisions12);
  ASSERT_FALSE(revisions12 == revisions32);
  ASSERT_TRUE(revisions12 != revisions32);
  ASSERT_FALSE(revisions12 == revisions13);
  ASSERT_TRUE(revisions12 != revisions13);
}

TEST(QueryAnalyzerRevisionsTest, print) {
  darbotdb::QueryAnalyzerRevisions revisions(1, 2);
  std::stringstream ss;
  revisions.print(ss) << "tail";
  ASSERT_EQ("[Current:1 System:2]tail", ss.str());
}

TEST(QueryAnalyzerRevisionsTest, fillFullData) {
  darbotdb::QueryAnalyzerRevisions revisions(1, 2);
  VPackBuilder builder;
  {
    VPackObjectBuilder query(&builder);
    revisions.toVelocyPack(builder);
  }
  auto slice = builder.slice().get(
      darbotdb::StaticStrings::ArangoSearchAnalyzersRevision);
  ASSERT_TRUE(slice.isObject());
  auto current =
      slice.get(darbotdb::StaticStrings::ArangoSearchCurrentAnalyzersRevision);
  ASSERT_TRUE(current.isNumber());
  ASSERT_EQ(1, current.getNumber<darbotdb::AnalyzersRevision::Revision>());
  auto system =
      slice.get(darbotdb::StaticStrings::ArangoSearchSystemAnalyzersRevision);
  ASSERT_TRUE(system.isNumber());
  ASSERT_EQ(2, system.getNumber<darbotdb::AnalyzersRevision::Revision>());

  darbotdb::QueryAnalyzerRevisions revisions2;
  ASSERT_TRUE(revisions2.fromVelocyPack(builder.slice()).ok());
  ASSERT_TRUE(revisions == revisions2);
}

TEST(QueryAnalyzerRevisionsTest, allDefaultData) {
  darbotdb::QueryAnalyzerRevisions revisions;
  VPackBuilder builder;
  {
    VPackObjectBuilder query(&builder);
    revisions.toVelocyPack(builder);
  }
  auto slice = builder.slice().get(
      darbotdb::StaticStrings::ArangoSearchAnalyzersRevision);
  ASSERT_TRUE(slice.isEmptyObject());

  darbotdb::QueryAnalyzerRevisions revisions2;
  ASSERT_TRUE(revisions2.fromVelocyPack(builder.slice()).ok());
  ASSERT_TRUE(revisions == revisions2);
}

TEST(QueryAnalyzerRevisionsTest, fillSystemData) {
  darbotdb::QueryAnalyzerRevisions revisions(darbotdb::AnalyzersRevision::MIN,
                                             2);
  VPackBuilder builder;
  {
    VPackObjectBuilder query(&builder);
    revisions.toVelocyPack(builder);
  }
  auto slice = builder.slice().get(
      darbotdb::StaticStrings::ArangoSearchAnalyzersRevision);
  ASSERT_TRUE(slice.isObject());
  auto current =
      slice.get(darbotdb::StaticStrings::ArangoSearchCurrentAnalyzersRevision);
  ASSERT_TRUE(current.isNone());
  auto system =
      slice.get(darbotdb::StaticStrings::ArangoSearchSystemAnalyzersRevision);
  ASSERT_TRUE(system.isNumber());
  ASSERT_EQ(2, system.getNumber<darbotdb::AnalyzersRevision::Revision>());

  darbotdb::QueryAnalyzerRevisions revisions2;
  ASSERT_TRUE(revisions2.fromVelocyPack(builder.slice()).ok());
  ASSERT_TRUE(revisions == revisions2);
}

TEST(QueryAnalyzerRevisionsTest, fillCurrentData) {
  darbotdb::QueryAnalyzerRevisions revisions(1,
                                             darbotdb::AnalyzersRevision::MIN);
  VPackBuilder builder;
  {
    VPackObjectBuilder query(&builder);
    revisions.toVelocyPack(builder);
  }
  auto slice = builder.slice().get(
      darbotdb::StaticStrings::ArangoSearchAnalyzersRevision);
  ASSERT_TRUE(slice.isObject());
  auto current =
      slice.get(darbotdb::StaticStrings::ArangoSearchCurrentAnalyzersRevision);
  ASSERT_TRUE(current.isNumber());
  ASSERT_EQ(1, current.getNumber<darbotdb::AnalyzersRevision::Revision>());
  auto system =
      slice.get(darbotdb::StaticStrings::ArangoSearchSystemAnalyzersRevision);
  ASSERT_TRUE(system.isNone());

  darbotdb::QueryAnalyzerRevisions revisions2;
  ASSERT_TRUE(revisions2.fromVelocyPack(builder.slice()).ok());
  ASSERT_TRUE(revisions == revisions2);
}
