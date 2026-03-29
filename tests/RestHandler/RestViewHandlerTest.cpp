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

#include "gtest/gtest.h"

#include "velocypack/Builder.h"
#include "velocypack/Parser.h"

#include "IResearch/RestHandlerMock.h"
#include "IResearch/common.h"
#include "Mocks/LogLevels.h"
#include "Mocks/Servers.h"
#include "Mocks/StorageEngineMock.h"

#include "Aql/QueryRegistry.h"
#include "Auth/UserManagerMock.h"
#include "Basics/DownCast.h"
#include "GeneralServer/AuthenticationFeature.h"
#include "RestHandler/RestViewHandler.h"
#include "RestServer/DatabaseFeature.h"
#include "RestServer/ViewTypesFeature.h"

namespace {
struct TestView : darbotdb::LogicalView {
  static constexpr auto typeInfo() noexcept {
    return std::pair{static_cast<darbotdb::ViewType>(42),
                     std::string_view{"testViewType"}};
  }

  darbotdb::Result _appendVelocyPackResult;
  darbotdb::velocypack::Builder _properties;

  TestView(TRI_vocbase_t& vocbase,
           darbotdb::velocypack::Slice const& definition)
      : LogicalView(*this, vocbase, definition, false) {}
  darbotdb::Result appendVPackImpl(darbotdb::velocypack::Builder& build,
                                   Serialization, bool) const override {
    build.add("properties", _properties.slice());
    return _appendVelocyPackResult;
  }
  darbotdb::Result dropImpl() override {
    return darbotdb::storage_helper::drop(*this);
  }
  void open() override {}
  darbotdb::Result renameImpl(std::string const& oldName) override {
    return darbotdb::storage_helper::rename(*this, oldName);
  }
  darbotdb::Result properties(darbotdb::velocypack::Slice properties,
                              bool isUserRequest,
                              bool /*partialUpdate*/) override {
    EXPECT_TRUE(isUserRequest);
    _properties = darbotdb::velocypack::Builder(properties);
    return darbotdb::Result();
  }
  bool visitCollections(CollectionVisitor const& /*visitor*/) const override {
    return true;
  }
};

struct ViewFactory : darbotdb::ViewFactory {
  darbotdb::Result create(darbotdb::LogicalView::ptr& view,
                          TRI_vocbase_t& vocbase,
                          darbotdb::velocypack::Slice definition,
                          bool isUserRequest) const override {
    EXPECT_TRUE(isUserRequest);
    view = vocbase.createView(definition, isUserRequest);

    return darbotdb::Result();
  }

  darbotdb::Result instantiate(darbotdb::LogicalView::ptr& view,
                               TRI_vocbase_t& vocbase,
                               darbotdb::velocypack::Slice definition,
                               bool /*isUserRequest*/) const override {
    view = std::make_shared<TestView>(vocbase, definition);

    return darbotdb::Result();
  }
};

}  // namespace

class RestViewHandlerTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 protected:
  darbotdb::tests::mocks::MockAqlServer server;
  ViewFactory viewFactory;

  RestViewHandlerTest() {
    expectUserManagerCalls();
    auto& viewTypesFeature = server.getFeature<darbotdb::ViewTypesFeature>();
    viewTypesFeature.emplace(TestView::typeInfo().second, viewFactory);
  }

  void expectUserManagerCalls() {
    using namespace darbotdb;
    auto* authFeature = AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();
    auto* um =
        dynamic_cast<testing::StrictMock<auth::UserManagerMock>*>(userManager);
    EXPECT_NE(um, nullptr);

    using namespace ::testing;
    EXPECT_CALL(*um, databaseAuthLevel)
        .Times(AtLeast(1))
        .WillRepeatedly(WithArgs<0, 1>(
            [this](std::string const& username, std::string const& dbname) {
              if (_userMap.empty()) {
                return auth::Level::NONE;
              }
              auto const it = _userMap.find(username);
              EXPECT_NE(it, _userMap.end());
              return it->second.databaseAuthLevel(dbname);
            }));
    EXPECT_CALL(*um, setAuthInfo)
        .Times(AtLeast(1))
        .WillRepeatedly(
            [this](auth::UserMap const& userMap) { _userMap = userMap; });
  }
  darbotdb::auth::UserMap _userMap;
};

TEST_F(RestViewHandlerTest, test_auth) {
  // test create
  {
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.setRequestType(darbotdb::rest::RequestType::POST);
    request._payload.openObject();
    request._payload.add(darbotdb::StaticStrings::DataSourceName,
                         darbotdb::velocypack::Value("testView"));
    request._payload.add(darbotdb::StaticStrings::DataSourceType,
                         darbotdb::velocypack::Value("testViewType"));
    request._payload.close();

    EXPECT_TRUE(vocbase.views().empty());

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      EXPECT_TRUE(vocbase.views().empty());
    }

    // not authorized (RO user)
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      EXPECT_TRUE(vocbase.views().empty());
    }

    // authorzed (RW user)
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::CREATED, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
    }
  }

  // test drop
  {
    auto createViewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"testViewType\" }");
    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto logicalView = vocbase.createView(createViewJson->slice(), false);
    ASSERT_FALSE(!logicalView);
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.addSuffix("testView");
    request.setRequestType(darbotdb::rest::RequestType::DELETE_REQ);

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
    }

    // not authorized (RO user database)
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
    }

    // authorized (NONE user view) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
      user.grantCollection(vocbase.name(), "testView",
                           darbotdb::auth::Level::NONE);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((slice.hasKey("result") && slice.get("result").isBoolean() &&
                   true == slice.get("result").getBoolean()));
      EXPECT_TRUE(vocbase.views().empty());
    }
  }

  // test rename
  {
    auto createViewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"testViewType\" }");
    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto logicalView = vocbase.createView(createViewJson->slice(), false);
    ASSERT_FALSE(!logicalView);
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.addSuffix("testView");
    request.addSuffix("rename");
    request.setRequestType(darbotdb::rest::RequestType::PUT);
    request._payload.openObject();
    request._payload.add("name", darbotdb::velocypack::Value("testView1"));
    request._payload.close();

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
      auto view1 = vocbase.lookupView("testView1");
      EXPECT_FALSE(view1);
    }

    // not authorized (RO user database)
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
      auto view1 = vocbase.lookupView("testView1");
      EXPECT_FALSE(view1);
    }

    // not authorized (NONE user view with failing toVelocyPack()) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
      user.grantCollection(vocbase.name(), "testView",
                           darbotdb::auth::Level::NONE);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database
      auto* testView = darbotdb::basics::downCast<TestView>(logicalView.get());
      testView->_appendVelocyPackResult = darbotdb::Result(TRI_ERROR_FORBIDDEN);
      auto resetAppendVelocyPackResult =
          std::shared_ptr<TestView>(testView, [](TestView* p) -> void {
            p->_appendVelocyPackResult = darbotdb::Result();
          });

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
      auto view1 = vocbase.lookupView("testView1");
      EXPECT_FALSE(view1);
    }

    // authorized (NONE user view) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
      user.grantCollection(vocbase.name(), "testView",
                           darbotdb::auth::Level::NONE);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView1") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(view);
      auto view1 = vocbase.lookupView("testView1");
      EXPECT_FALSE(!view1);
    }
  }

  // test modify
  {
    auto createViewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"testViewType\" }");
    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto logicalView = vocbase.createView(createViewJson->slice(), false);
    ASSERT_FALSE(!logicalView);
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.addSuffix("testView");
    request.addSuffix("properties");
    request.setRequestType(darbotdb::rest::RequestType::PUT);
    request._payload.openObject();
    request._payload.add("key", darbotdb::velocypack::Value("value"));
    request._payload.close();

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
    }

    // not authorized (RO user database)
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
    }

    // not authorized (NONE user view with failing toVelocyPack()) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
      user.grantCollection(vocbase.name(), "testView",
                           darbotdb::auth::Level::NONE);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database
      auto* testView = darbotdb::basics::downCast<TestView>(logicalView.get());
      testView->_appendVelocyPackResult = darbotdb::Result(TRI_ERROR_INTERNAL);
      auto resetAppendVelocyPackResult =
          std::shared_ptr<TestView>(testView, [](TestView* p) -> void {
            p->_appendVelocyPackResult = darbotdb::Result();
          });

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::SERVER_ERROR,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::SERVER_ERROR) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_INTERNAL ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
      slice = darbotdb::basics::downCast<TestView>(*view)._properties.slice();
      EXPECT_FALSE(slice.isObject());
    }

    // authorized (NONE user view) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
      user.grantCollection(vocbase.name(), "testView",
                           darbotdb::auth::Level::NONE);
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
      EXPECT_TRUE((slice.hasKey("properties") &&
                   slice.get("properties").isObject() &&
                   slice.get("properties").hasKey("key") &&
                   slice.get("properties").get("key").isString() &&
                   std::string("value") ==
                       slice.get("properties").get("key").copyString()));
      auto view = vocbase.lookupView("testView");
      EXPECT_FALSE(!view);
      slice = darbotdb::basics::downCast<TestView>(*view)._properties.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((slice.hasKey("key") && slice.get("key").isString() &&
                   std::string("value") == slice.get("key").copyString()));
    }
    /* redundant because of above
        // not authorized (RO user view)
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::RO); userManager->setAuthInfo(userMap); // set
       user map to avoid loading configuration from system database

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
       responce.responseCode()); auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Code) &&
       slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
       size_t(darbotdb::rest::ResponseCode::FORBIDDEN),
       slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Error) &&
       slice.get(darbotdb::StaticStrings::Error).isBoolean() && true,
       slice.get(darbotdb::StaticStrings::Error).getBoolean());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
       slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
       TRI_ERROR_FORBIDDEN,
       slice.get(darbotdb::StaticStrings::ErrorNum).getNumber<int>()); auto view
       = vocbase.lookupView("testView"); EXPECT_FALSE(!view);
        }

        // not authorized (RW user view with failing toVelocyPack())
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::RW); userManager->setAuthInfo(userMap); // set
       user map to avoid loading configuration from system database auto*
       testView = darbotdb::basics::downCast<TestView>(logicalView.get());
          testView->_appendVelocyPackResult =
       darbotdb::Result(TRI_ERROR_INTERNAL); auto resetAppendVelocyPackResult =
       std::shared_ptr<TestView>(testView, [](TestView* p)->void {
       p->_appendVelocyPackResult = darbotdb::Result(); });

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::SERVER_ERROR,
       responce.responseCode()); auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Code) &&
       slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
       size_t(darbotdb::rest::ResponseCode::SERVER_ERROR),
       slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Error) &&
       slice.get(darbotdb::StaticStrings::Error).isBoolean() && true,
       slice.get(darbotdb::StaticStrings::Error).getBoolean());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
       slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
       TRI_ERROR_INTERNAL,
       slice.get(darbotdb::StaticStrings::ErrorNum).getNumber<int>()); auto view
       = vocbase.lookupView("testView"); EXPECT_FALSE(!view); slice =
       darbotdb::basics::downCast<TestView>(*view)._properties.slice();
          EXPECT_FALSE(slice.isObject());
        }

        // authorzed (RW user)
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RW);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::RW); userManager->setAuthInfo(userMap); // set
       user map to avoid loading configuration from system database

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
          auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
       slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
       std::string("testView"),
       slice.get(darbotdb::StaticStrings::DataSourceName).copyString());
          EXPECT_EQ(slice.hasKey("properties") &&
       slice.get("properties").isObject() &&
       slice.get("properties").hasKey("key") &&
       slice.get("properties").get("key").isString() && std::string("value"),
       slice.get("properties").get("key").copyString()); auto view =
       vocbase.lookupView("testView"); EXPECT_FALSE(!view); slice =
       darbotdb::basics::downCast<TestView>(*view)._properties.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey("key") && slice.get("key").isString() &&
       std::string("value"), slice.get("key").copyString());
        }
    */
  }

  // test get view (basic)
  {
    auto createViewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"testViewType\" }");
    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto logicalView = vocbase.createView(createViewJson->slice(), false);
    ASSERT_FALSE(!logicalView);
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.addSuffix("testView");
    request.setRequestType(darbotdb::rest::RequestType::GET);

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
    }

    // not authorized (failed detailed toVelocyPack(...)) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      user.grantCollection(
          vocbase.name(), "testView",
          darbotdb::auth::Level::NONE);   // for missing collections
                                          // User::collectionAuthLevel(...)
                                          // returns database auth::Level
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database
      auto* testView = darbotdb::basics::downCast<TestView>(logicalView.get());
      testView->_appendVelocyPackResult = darbotdb::Result(TRI_ERROR_FORBIDDEN);
      auto resetAppendVelocyPackResult =
          std::shared_ptr<TestView>(testView, [](TestView* p) -> void {
            p->_appendVelocyPackResult = darbotdb::Result();
          });

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
    }

    // authorized (NONE view) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      user.grantCollection(
          vocbase.name(), "testView",
          darbotdb::auth::Level::NONE);   // for missing collections
                                          // User::collectionAuthLevel(...)
                                          // returns database auth::Level
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
    }
    /* redundant because of above
        // not authorized (NONE view)
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::NONE); // for missing collections
       User::collectionAuthLevel(...) returns database auth::Level
          userManager->setAuthInfo(userMap); // set user map to avoid loading
       configuration from system database

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
       responce.responseCode()); auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Code) &&
       slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
       size_t(darbotdb::rest::ResponseCode::FORBIDDEN),
       slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Error) &&
       slice.get(darbotdb::StaticStrings::Error).isBoolean() && true,
       slice.get(darbotdb::StaticStrings::Error).getBoolean());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
       slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
       TRI_ERROR_FORBIDDEN,
       slice.get(darbotdb::StaticStrings::ErrorNum).getNumber<int>());
        }

        // authorzed (RO view)
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::RO); userManager->setAuthInfo(userMap); // set
       user map to avoid loading configuration from system database

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
          auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
       slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
       std::string("testView"),
       slice.get(darbotdb::StaticStrings::DataSourceName).copyString());
        }
    */
  }

  // test get view (detailed)
  {
    auto createViewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView\", \"type\": \"testViewType\" }");
    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto logicalView = vocbase.createView(createViewJson->slice(), false);
    ASSERT_FALSE(!logicalView);
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.addSuffix("testView");
    request.addSuffix("properties");
    request.setRequestType(darbotdb::rest::RequestType::GET);

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
    }

    // not authorized (failed detailed toVelocyPack(...))
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      user.grantCollection(
          vocbase.name(), "testView",
          darbotdb::auth::Level::NONE);   // for missing collections
                                          // User::collectionAuthLevel(...)
                                          // returns database auth::Level
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database
      auto* testView = darbotdb::basics::downCast<TestView>(logicalView.get());
      testView->_appendVelocyPackResult = darbotdb::Result(TRI_ERROR_FORBIDDEN);
      auto resetAppendVelocyPackResult =
          std::shared_ptr<TestView>(testView, [](TestView* p) -> void {
            p->_appendVelocyPackResult = darbotdb::Result();
          });

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
    }

    // authorized (NONE view) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      user.grantCollection(
          vocbase.name(), "testView",
          darbotdb::auth::Level::NONE);   // for missing collections
                                          // User::collectionAuthLevel(...)
                                          // returns database auth::Level
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
    }
    /* redundant because of above
        // not authorized (NONE view)
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::NONE); // for missing collections
       User::collectionAuthLevel(...) returns database auth::Level
          userManager->setAuthInfo(userMap); // set user map to avoid loading
       configuration from system database

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
       responce.responseCode()); auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Code) &&
       slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
       size_t(darbotdb::rest::ResponseCode::FORBIDDEN),
       slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::Error) &&
       slice.get(darbotdb::StaticStrings::Error).isBoolean() && true,
       slice.get(darbotdb::StaticStrings::Error).getBoolean());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
       slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
       TRI_ERROR_FORBIDDEN,
       slice.get(darbotdb::StaticStrings::ErrorNum).getNumber<int>());
        }

        // authorzed (RO view)
        {
          darbotdb::auth::UserMap userMap;
          auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", "",
       darbotdb::auth::Source::LDAP)).first->second;
          user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
          user.grantCollection(vocbase.name(), "testView",
       darbotdb::auth::Level::RO); userManager->setAuthInfo(userMap); // set
       user map to avoid loading configuration from system database

          auto status = handler.execute();
          EXPECT_EQ(darbotdb::RestStatus::DONE, status);
          EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
          auto slice = responce._payload.slice();
          EXPECT_TRUE(slice.isObject());
          EXPECT_EQ(slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
       slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
       std::string("testView"),
       slice.get(darbotdb::StaticStrings::DataSourceName).copyString());
        }
    */
  }

  // test get all views
  {
    auto createView1Json = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView1\", \"type\": \"testViewType\" }");
    auto createView2Json = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testView2\", \"type\": \"testViewType\" }");
    TRI_vocbase_t vocbase(testDBInfo(server.server()));
    auto logicalView1 = vocbase.createView(createView1Json->slice(), false);
    ASSERT_FALSE(!logicalView1);
    auto logicalView2 = vocbase.createView(createView2Json->slice(), false);
    ASSERT_FALSE(!logicalView2);
    auto requestPtr = std::make_unique<GeneralRequestMock>(vocbase);
    auto& request = *requestPtr;
    auto responcePtr = std::make_unique<GeneralResponseMock>();
    auto& responce = *responcePtr;
    darbotdb::RestViewHandler handler(server.server(), requestPtr.release(),
                                      responcePtr.release());

    request.setRequestType(darbotdb::rest::RequestType::GET);

    struct ExecContext : public darbotdb::ExecContext {
      ExecContext()
          : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                  darbotdb::ExecContext::Type::Default, "", "",
                                  darbotdb::auth::Level::NONE,
                                  darbotdb::auth::Level::NONE, false) {}
    };
    auto execContext = std::make_shared<ExecContext>();
    darbotdb::ExecContextScope execContextScope(execContext);
    auto* authFeature = darbotdb::AuthenticationFeature::instance();
    auto* userManager = authFeature->userManager();

    // not authorized (missing user)
    {
      darbotdb::auth::UserMap userMap;  // empty map, no user -> no permissions
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::FORBIDDEN,
                responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::FORBIDDEN) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
           slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
           TRI_ERROR_FORBIDDEN ==
               ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                             .getNumber<int>()}));
    }

    // not authorized (failed detailed toVelocyPack(...)) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      user.grantCollection(
          vocbase.name(), "testView1",
          darbotdb::auth::Level::NONE);  // for missing collections
                                         // User::collectionAuthLevel(...)
                                         // returns database auth::Level
      user.grantCollection(
          vocbase.name(), "testView2",
          darbotdb::auth::Level::NONE);   // for missing collections
                                          // User::collectionAuthLevel(...)
                                          // returns database auth::Level
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database
      auto* testView = darbotdb::basics::downCast<TestView>(logicalView2.get());
      testView->_appendVelocyPackResult = darbotdb::Result(TRI_ERROR_FORBIDDEN);
      auto resetAppendVelocyPackResult =
          std::shared_ptr<TestView>(testView, [](TestView* p) -> void {
            p->_appendVelocyPackResult = darbotdb::Result();
          });

      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::OK) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           false == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(slice.hasKey("result"));
      slice = slice.get("result");
      EXPECT_TRUE(slice.isArray());
      EXPECT_EQ(1U, slice.length());
      slice = slice.at(0);
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView1") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
    }

    // authorized (NONE view) as per
    // https://github.com/darbotdb/backlog/issues/459
    {
      darbotdb::auth::UserMap userMap;
      auto& user = userMap.emplace("", darbotdb::auth::User::newUser("", ""))
                       .first->second;
      user.grantDatabase(vocbase.name(), darbotdb::auth::Level::RO);
      user.grantCollection(
          vocbase.name(), "testView1",
          darbotdb::auth::Level::NONE);   // for missing collections
                                          // User::collectionAuthLevel(...)
                                          // returns database auth::Level
      userManager->setAuthInfo(userMap);  // set user map to avoid loading
                                          // configuration from system database

      vocbase.dropView(
          logicalView2->id(),
          true);  // remove second view to make test result deterministic
      auto status = handler.execute();
      EXPECT_EQ(darbotdb::RestStatus::DONE, status);
      EXPECT_EQ(darbotdb::rest::ResponseCode::OK, responce.responseCode());
      auto slice = responce._payload.slice();
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Code) &&
           slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
           size_t(darbotdb::rest::ResponseCode::OK) ==
               slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
      EXPECT_TRUE(
          (slice.hasKey(darbotdb::StaticStrings::Error) &&
           slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
           false == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
      EXPECT_TRUE(slice.hasKey("result"));
      slice = slice.get("result");
      EXPECT_TRUE(slice.isArray());
      EXPECT_EQ(1U, slice.length());
      slice = slice.at(0);
      EXPECT_TRUE(slice.isObject());
      EXPECT_TRUE((
          slice.hasKey(darbotdb::StaticStrings::DataSourceName) &&
          slice.get(darbotdb::StaticStrings::DataSourceName).isString() &&
          std::string("testView1") ==
              slice.get(darbotdb::StaticStrings::DataSourceName).copyString()));
    }
  }
}
