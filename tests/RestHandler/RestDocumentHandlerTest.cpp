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
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#include "gtest/gtest.h"

#include "IResearch/RestHandlerMock.h"
#include "Mocks/LogLevels.h"
#include "Mocks/Servers.h"

#include "Basics/StaticStrings.h"
#include "Cluster/ServerState.h"
#include "RestHandler/RestDocumentHandler.h"

using namespace darbotdb;

struct RoleChanger {
  explicit RoleChanger(ServerState::RoleEnum newRole)
      : oldRole(ServerState::instance()->getRole()) {
    ServerState::instance()->setRole(newRole);
  }
  ~RoleChanger() { ServerState::instance()->setRole(oldRole); }
  ServerState::RoleEnum const oldRole;
};

class RestDocumentHandlerTestBase
    : public darbotdb::tests::LogSuppressor<darbotdb::Logger::CLUSTER,
                                            darbotdb::LogLevel::WARN> {
 protected:
  darbotdb::tests::mocks::MockRestServer server;

  RestDocumentHandlerTestBase() : server{} {}

  ~RestDocumentHandlerTestBase() = default;
};

class RestDocumentHandlerTest : public RestDocumentHandlerTestBase,
                                public ::testing::Test {
 protected:
  RestDocumentHandlerTest() : RestDocumentHandlerTestBase{} {}

  ~RestDocumentHandlerTest() = default;
};

class RestDocumentHandlerLaneTest
    : public RestDocumentHandlerTestBase,
      public ::testing::TestWithParam<darbotdb::rest::RequestType> {
 protected:
  RestDocumentHandlerLaneTest()
      : RestDocumentHandlerTestBase{}, _type(GetParam()) {}

  ~RestDocumentHandlerLaneTest() = default;

  darbotdb::rest::RequestType _type;
};

TEST_P(RestDocumentHandlerLaneTest, test_request_lane_user) {
  auto& vocbase = server.getSystemDatabase();
  auto fakeRequest = std::make_unique<GeneralRequestMock>(vocbase);
  auto fakeResponse = std::make_unique<GeneralResponseMock>();
  fakeRequest->setRequestType(_type);

  RoleChanger roleChanger(ServerState::ROLE_DBSERVER);
  darbotdb::RestDocumentHandler testee(server.server(), fakeRequest.release(),
                                       fakeResponse.release());
  ASSERT_EQ(darbotdb::RequestLane::CLIENT_SLOW, testee.lane());
}

TEST_P(RestDocumentHandlerLaneTest, test_request_lane_coordinator) {
  auto& vocbase = server.getSystemDatabase();
  auto fakeRequest = std::make_unique<GeneralRequestMock>(vocbase);
  auto fakeResponse = std::make_unique<GeneralResponseMock>();
  fakeRequest->setRequestType(_type);

  RoleChanger roleChanger(ServerState::ROLE_COORDINATOR);
  darbotdb::RestDocumentHandler testee(server.server(), fakeRequest.release(),
                                       fakeResponse.release());
  ASSERT_EQ(darbotdb::RequestLane::CLIENT_SLOW, testee.lane());
}

TEST_P(RestDocumentHandlerLaneTest, test_request_lane_replication) {
  auto& vocbase = server.getSystemDatabase();
  auto fakeRequest = std::make_unique<GeneralRequestMock>(vocbase);
  auto fakeResponse = std::make_unique<GeneralResponseMock>();
  fakeRequest->setRequestType(_type);
  fakeRequest->values().emplace(
      darbotdb::StaticStrings::IsSynchronousReplicationString, "abc");

  RoleChanger roleChanger(ServerState::ROLE_DBSERVER);
  darbotdb::RestDocumentHandler testee(server.server(), fakeRequest.release(),
                                       fakeResponse.release());

  if (_type == darbotdb::rest::RequestType::GET) {
    ASSERT_EQ(darbotdb::RequestLane::CLIENT_SLOW, testee.lane());
  } else {
    ASSERT_EQ(darbotdb::RequestLane::SERVER_SYNCHRONOUS_REPLICATION,
              testee.lane());
  }
}

INSTANTIATE_TEST_CASE_P(
    RequestTypeVariations, RestDocumentHandlerLaneTest,
    ::testing::Values(darbotdb::rest::RequestType::GET,
                      darbotdb::rest::RequestType::PUT,
                      darbotdb::rest::RequestType::POST,
                      darbotdb::rest::RequestType::DELETE_REQ,
                      darbotdb::rest::RequestType::PATCH));
