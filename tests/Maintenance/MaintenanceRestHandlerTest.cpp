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
/// @author Kaveh Vahedipour
/// @author Matthew Von-Maszewski
/// @author Copyright 2017-2018, darbotdb GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "gtest/gtest.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "Cluster/MaintenanceRestHandler.h"
#include "Endpoint/ConnectionInfo.h"
#include "Rest/HttpRequest.h"
#include "Rest/HttpResponse.h"

#include <velocypack/Buffer.h>
#include <velocypack/Builder.h>
#include <velocypack/Iterator.h>

// give access to some protected routines for more thorough unit tests
class TestHandler : public darbotdb::MaintenanceRestHandler {
 public:
  TestHandler(darbotdb::ArangodServer& server, darbotdb::GeneralRequest* req,
              darbotdb::GeneralResponse* res)
      : darbotdb::MaintenanceRestHandler(server, req, res){};

  bool test_parsePutBody(VPackSlice const& parameters) {
    return parsePutBody(parameters);
  }

};  // class TestHandler

TEST(MaintenanceRestHandler, parse_rest_put) {
  VPackBuffer<uint8_t> buffer;
  VPackBuilder body(buffer);

  // intentionally building this in non-alphabetic order, and name not first
  //  {"name":"CreateCollection","collection":"a","database":"test","properties":{"waitForSync":true}}
  {
    VPackObjectBuilder b(&body);
    body.add("database", VPackValue("test"));
    body.add("name", VPackValue("CreateCollection"));
    body.add(VPackValue("properties"));
    {
      VPackObjectBuilder bb(&body);
      body.add("waitForSync", VPackValue(true));
    }
    body.add("collection", VPackValue("a"));
  }

  auto* dummyRequest = new darbotdb::HttpRequest(darbotdb::ConnectionInfo(), 1);
  dummyRequest->setDefaultContentType();  // JSON
  dummyRequest->setPayload(buffer);
  dummyRequest->setRequestType(darbotdb::rest::RequestType::PUT);

  auto* dummyResponse = new darbotdb::HttpResponse(
      darbotdb::rest::ResponseCode::OK, 1, nullptr,
      darbotdb::rest::ResponseCompressionType::kNoCompression);
  darbotdb::ArangodServer dummyServer{nullptr, nullptr};
  TestHandler dummyHandler(dummyServer, dummyRequest, dummyResponse);

  ASSERT_TRUE(dummyHandler.test_parsePutBody(body.slice()));
  ASSERT_TRUE(dummyHandler.getActionDesc().has("name"));
  ASSERT_EQ(dummyHandler.getActionDesc().get("name"), "CreateCollection");
  ASSERT_TRUE(dummyHandler.getActionDesc().has("collection"));
  ASSERT_EQ(dummyHandler.getActionDesc().get("collection"), "a");
  ASSERT_TRUE(dummyHandler.getActionDesc().has("database"));
  ASSERT_EQ(dummyHandler.getActionDesc().get("database"), "test");

  VPackObjectIterator it(dummyHandler.getActionProp().slice(), true);
  ASSERT_EQ(it.key().copyString(), "waitForSync");
  ASSERT_EQ(it.value().getBoolean(), true);
}
