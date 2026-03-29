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

#include "velocypack/Parser.h"

#include "IResearch/RestHandlerMock.h"
#include "IResearch/common.h"
#include "Mocks/LogLevels.h"
#include "Mocks/Servers.h"
#include "Mocks/StorageEngineMock.h"

#include "Aql/QueryRegistry.h"
#include "Auth/UserManagerMock.h"
#include "Basics/StaticStrings.h"
#include "GeneralServer/AuthenticationFeature.h"
#include "RestHandler/RestUsersHandler.h"
#include "RestServer/DatabaseFeature.h"
#include "RestServer/SystemDatabaseFeature.h"
#include "RestServer/ViewTypesFeature.h"
#include "RestServer/VocbaseContext.h"
#include "Utils/ExecContext.h"
#ifdef USE_V8
#include "V8Server/V8DealerFeature.h"
#endif
#include "VocBase/LogicalCollection.h"
#include "VocBase/LogicalView.h"
#include "VocBase/vocbase.h"

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
      : darbotdb::LogicalView(*this, vocbase, definition, false) {}
  darbotdb::Result appendVPackImpl(darbotdb::velocypack::Builder& build,
                                   Serialization, bool) const override {
    build.add("properties", _properties.slice());
    return _appendVelocyPackResult;
  }
  darbotdb::Result dropImpl() override { return darbotdb::Result(); }
  void open() override {}
  darbotdb::Result renameImpl(std::string const&) override {
    return darbotdb::Result();
  }
  darbotdb::Result properties(darbotdb::velocypack::Slice properties,
                              bool isUserRequest,
                              bool /*partialUpdate*/) override {
    EXPECT_TRUE(isUserRequest);
    _properties = darbotdb::velocypack::Builder(properties);
    return darbotdb::Result();
  }
  bool visitCollections(CollectionVisitor const&) const override {
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

class RestUsersHandlerTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 protected:
  darbotdb::tests::mocks::MockAqlServer server;
  darbotdb::SystemDatabaseFeature::ptr system;
  ViewFactory viewFactory;

  RestUsersHandlerTest()
      : server(),
        system(server.getFeature<darbotdb::SystemDatabaseFeature>().use()) {
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
    EXPECT_CALL(*um, storeUser)
        .Times(AtLeast(1))
        .WillRepeatedly([this](bool const replace, std::string const& username,
                               std::string const& pass, bool const active,
                               velocypack::Slice const extras) {
          auto user = auth::User::newUser(username, pass);
          user.setActive(active);
          if (extras.isObject() && !extras.isEmptyObject()) {
            user.setUserData(VPackBuilder(extras));
          }
          const auto it = _userMap.find(username);
          EXPECT_NE(replace, it == _userMap.end());
          if (replace) {
            it->second = user;
          } else {
            _userMap.emplace(username, user);
          }
          return Result{};
        });
    EXPECT_CALL(*um, accessUser)
        .Times(AtLeast(1))
        .WillRepeatedly([this](std::string const& username,
                               auth::UserManager::ConstUserCallback&& cb) {
          const auto it = _userMap.find(username);
          EXPECT_NE(it, _userMap.end());
          auto const r = cb(it->second);
          EXPECT_TRUE(r.ok());
          return Result{};
        });
    EXPECT_CALL(*um, updateUser)
        .Times(AtLeast(1))
        .WillRepeatedly([this](std::string const& username,
                               auth::UserManager::UserCallback&& cb,
                               auth::UserManager::RetryOnConflict const) {
          const auto it = _userMap.find(username);
          EXPECT_NE(it, _userMap.end());
          auto const r = cb(it->second);
          EXPECT_TRUE(r.ok());
          return Result{};
        });
    EXPECT_CALL(*um, collectionAuthLevel)
        .Times(AtLeast(1))
        .WillRepeatedly(WithArgs<0, 1, 2>([this](std::string const& username,
                                                 std::string const& dbname,
                                                 std::string_view cname) {
          auto const it = _userMap.find(username);
          EXPECT_NE(it, _userMap.end());
          EXPECT_EQ(username, it->second.username());
          return it->second.collectionAuthLevel(dbname, cname);
        }));
    EXPECT_CALL(*um, setAuthInfo)
        .Times(AtLeast(1))
        .WillRepeatedly(WithArgs<0>(
            [this](auth::UserMap const& userMap) { _userMap = userMap; }));
  }
  darbotdb::auth::UserMap _userMap;
};

TEST_F(RestUsersHandlerTest, test_collection_auth) {
  auto* authFeature = darbotdb::AuthenticationFeature::instance();
  auto* userManager = authFeature->userManager();

  static const std::string userName("testUser");
  auto& databaseFeature = server.getFeature<darbotdb::DatabaseFeature>();
  TRI_vocbase_t* vocbase;  // will be owned by DatabaseFeature
  ASSERT_TRUE(
      databaseFeature.createDatabase(testDBInfo(server.server()), vocbase)
          .ok());
  auto grantRequestPtr = std::make_unique<GeneralRequestMock>(*vocbase);
  auto& grantRequest = *grantRequestPtr;
  auto grantResponcePtr = std::make_unique<GeneralResponseMock>();
  auto& grantResponce = *grantResponcePtr;
  auto grantWildcardRequestPtr = std::make_unique<GeneralRequestMock>(*vocbase);
  auto& grantWildcardRequest = *grantWildcardRequestPtr;
  auto grantWildcardResponcePtr = std::make_unique<GeneralResponseMock>();
  auto& grantWildcardResponce = *grantWildcardResponcePtr;
  auto revokeRequestPtr = std::make_unique<GeneralRequestMock>(*vocbase);
  auto& revokeRequest = *revokeRequestPtr;
  auto revokeResponcePtr = std::make_unique<GeneralResponseMock>();
  auto& revokeResponce = *revokeResponcePtr;
  auto revokeWildcardRequestPtr =
      std::make_unique<GeneralRequestMock>(*vocbase);
  auto& revokeWildcardRequest = *revokeWildcardRequestPtr;
  auto revokeWildcardResponcePtr = std::make_unique<GeneralResponseMock>();
  auto& revokeWildcardResponce = *revokeWildcardResponcePtr;
  darbotdb::RestUsersHandler grantHandler(
      server.server(), grantRequestPtr.release(), grantResponcePtr.release());
  darbotdb::RestUsersHandler grantWildcardHandler(
      server.server(), grantWildcardRequestPtr.release(),
      grantWildcardResponcePtr.release());
  darbotdb::RestUsersHandler revokeHandler(
      server.server(), revokeRequestPtr.release(), revokeResponcePtr.release());
  darbotdb::RestUsersHandler revokeWildcardHandler(
      server.server(), revokeWildcardRequestPtr.release(),
      revokeWildcardResponcePtr.release());

  grantRequest.addSuffix("testUser");
  grantRequest.addSuffix("database");
  grantRequest.addSuffix(vocbase->name());
  grantRequest.addSuffix("testDataSource");
  grantRequest.setRequestType(darbotdb::rest::RequestType::PUT);
  grantRequest._payload.openObject();
  grantRequest._payload.add(
      "grant", darbotdb::velocypack::Value(darbotdb::auth::convertFromAuthLevel(
                   darbotdb::auth::Level::RW)));
  grantRequest._payload.close();

  grantWildcardRequest.addSuffix("testUser");
  grantWildcardRequest.addSuffix("database");
  grantWildcardRequest.addSuffix(vocbase->name());
  grantWildcardRequest.addSuffix("*");
  grantWildcardRequest.setRequestType(darbotdb::rest::RequestType::PUT);
  grantWildcardRequest._payload.openObject();
  grantWildcardRequest._payload.add(
      "grant", darbotdb::velocypack::Value(darbotdb::auth::convertFromAuthLevel(
                   darbotdb::auth::Level::RW)));
  grantWildcardRequest._payload.close();

  revokeRequest.addSuffix("testUser");
  revokeRequest.addSuffix("database");
  revokeRequest.addSuffix(vocbase->name());
  revokeRequest.addSuffix("testDataSource");
  revokeRequest.setRequestType(darbotdb::rest::RequestType::DELETE_REQ);

  revokeWildcardRequest.addSuffix("testUser");
  revokeWildcardRequest.addSuffix("database");
  revokeWildcardRequest.addSuffix(vocbase->name());
  revokeWildcardRequest.addSuffix("*");
  revokeWildcardRequest.setRequestType(darbotdb::rest::RequestType::DELETE_REQ);

  struct ExecContext : public darbotdb::ExecContext {
    ExecContext()
        : darbotdb::ExecContext(darbotdb::ExecContext::ConstructorToken{},
                                darbotdb::ExecContext::Type::Default, userName,
                                "", darbotdb::auth::Level::RW,
                                darbotdb::auth::Level::NONE, true) {
    }  // ExecContext::isAdminUser() == true
  };
  auto execContext = std::make_shared<ExecContext>();
  darbotdb::ExecContextScope execContextScope(execContext);

  // test auth missing (grant)
  {
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);

    EXPECT_TRUE(
        (darbotdb::auth::Level::NONE ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = grantHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::NOT_FOUND,
              grantResponce.responseCode());
    auto slice = grantResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Code) &&
         slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
         size_t(darbotdb::rest::ResponseCode::NOT_FOUND) ==
             slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Error) &&
         slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
         true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
    EXPECT_TRUE((slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
                 slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
                 TRI_ERROR_DDB_DATA_SOURCE_NOT_FOUND ==
                     ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                                   .getNumber<int>()}));
    EXPECT_TRUE(
        (darbotdb::auth::Level::NONE ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
  }

  // test auth missing (revoke)
  {
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    userPtr->grantCollection(
        vocbase->name(), "testDataSource",
        darbotdb::auth::Level::RO);  // for missing collections
                                     // User::collectionAuthLevel(...) returns
                                     // database auth::Level

    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = revokeHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::NOT_FOUND,
              revokeResponce.responseCode());
    auto slice = revokeResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Code) &&
         slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
         size_t(darbotdb::rest::ResponseCode::NOT_FOUND) ==
             slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Error) &&
         slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
         true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
    EXPECT_TRUE((slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
                 slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
                 TRI_ERROR_DDB_DATA_SOURCE_NOT_FOUND ==
                     ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                                   .getNumber<int>()}));
    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(
             vocbase->name(), "testDataSource")));  // not modified from above
  }

  // test auth collection (grant)
  {
    auto collectionJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testDataSource\" }");
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    auto logicalCollection = std::shared_ptr<darbotdb::LogicalCollection>(
        vocbase->createCollection(collectionJson->slice()).get(),
        [vocbase](darbotdb::LogicalCollection* ptr) -> void {
          vocbase->dropCollection(ptr->id(), false);
        });
    ASSERT_FALSE(!logicalCollection);

    EXPECT_TRUE(
        (darbotdb::auth::Level::NONE ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = grantHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::OK, grantResponce.responseCode());
    auto slice = grantResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(vocbase->name() + "/testDataSource") &&
         slice.get(vocbase->name() + "/testDataSource").isString() &&
         darbotdb::auth::convertFromAuthLevel(darbotdb::auth::Level::RW) ==
             slice.get(vocbase->name() + "/testDataSource").copyString()));
    EXPECT_TRUE(
        (darbotdb::auth::Level::RW ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
  }

  // test auth collection (revoke)
  {
    auto collectionJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testDataSource\" }");
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    userPtr->grantCollection(
        vocbase->name(), "testDataSource",
        darbotdb::auth::Level::RO);  // for missing collections
                                     // User::collectionAuthLevel(...) returns
                                     // database auth::Level
    auto logicalCollection = std::shared_ptr<darbotdb::LogicalCollection>(
        vocbase->createCollection(collectionJson->slice()).get(),
        [vocbase](darbotdb::LogicalCollection* ptr) -> void {
          vocbase->dropCollection(ptr->id(), false);
        });
    ASSERT_FALSE(!logicalCollection);

    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = revokeHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::ACCEPTED,
              revokeResponce.responseCode());
    auto slice = revokeResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Code) &&
         slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
         size_t(darbotdb::rest::ResponseCode::ACCEPTED) ==
             slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Error) &&
         slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
         false == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
    // Granting collection-level access sets the Database level to UNDEFINED
    // (see User::grantCollection) after the collection-level access is revoked,
    // the DB level stays UNDEFINED
    EXPECT_TRUE(
        (darbotdb::auth::Level::UNDEFINED ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
  }

  // test auth view (grant)
  {
    auto viewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testDataSource\", \"type\": \"testViewType\" }");
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    auto logicalView = std::shared_ptr<darbotdb::LogicalView>(
        vocbase->createView(viewJson->slice(), false).get(),
        [vocbase](darbotdb::LogicalView* ptr) -> void {
          vocbase->dropView(ptr->id(), false);
        });
    ASSERT_FALSE(!logicalView);

    EXPECT_TRUE(
        (darbotdb::auth::Level::NONE ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = grantHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::NOT_FOUND,
              grantResponce.responseCode());
    auto slice = grantResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Code) &&
         slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
         size_t(darbotdb::rest::ResponseCode::NOT_FOUND) ==
             slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Error) &&
         slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
         true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
    EXPECT_TRUE((slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
                 slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
                 TRI_ERROR_DDB_DATA_SOURCE_NOT_FOUND ==
                     ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                                   .getNumber<int>()}));
    EXPECT_TRUE(
        (darbotdb::auth::Level::NONE ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
  }

  // test auth view (revoke)
  {
    auto viewJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testDataSource\", \"type\": \"testViewType\" }");
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    userPtr->grantCollection(
        vocbase->name(), "testDataSource",
        darbotdb::auth::Level::RO);  // for missing collections
                                     // User::collectionAuthLevel(...) returns
                                     // database auth::Level
    auto logicalView = std::shared_ptr<darbotdb::LogicalView>(
        vocbase->createView(viewJson->slice(), false).get(),
        [vocbase](darbotdb::LogicalView* ptr) -> void {
          vocbase->dropView(ptr->id(), false);
        });
    ASSERT_FALSE(!logicalView);

    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = revokeHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::NOT_FOUND,
              revokeResponce.responseCode());
    auto slice = revokeResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Code) &&
         slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
         size_t(darbotdb::rest::ResponseCode::NOT_FOUND) ==
             slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Error) &&
         slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
         true == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
    EXPECT_TRUE((slice.hasKey(darbotdb::StaticStrings::ErrorNum) &&
                 slice.get(darbotdb::StaticStrings::ErrorNum).isNumber<int>() &&
                 TRI_ERROR_DDB_DATA_SOURCE_NOT_FOUND ==
                     ErrorCode{slice.get(darbotdb::StaticStrings::ErrorNum)
                                   .getNumber<int>()}));
    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(
             vocbase->name(), "testDataSource")));  // not modified from above
  }

  // test auth wildcard (grant)
  {
    auto collectionJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testDataSource\" }");
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    auto logicalCollection = std::shared_ptr<darbotdb::LogicalCollection>(
        vocbase->createCollection(collectionJson->slice()).get(),
        [vocbase](darbotdb::LogicalCollection* ptr) -> void {
          vocbase->dropCollection(ptr->id(), false);
        });
    ASSERT_FALSE(!logicalCollection);

    EXPECT_TRUE(
        (darbotdb::auth::Level::NONE ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = grantWildcardHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_EQ(darbotdb::rest::ResponseCode::OK,
              grantWildcardResponce.responseCode());
    auto slice = grantWildcardResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(vocbase->name() + "/*") &&
         slice.get(vocbase->name() + "/*").isString() &&
         darbotdb::auth::convertFromAuthLevel(darbotdb::auth::Level::RW) ==
             slice.get(vocbase->name() + "/*").copyString()));
    EXPECT_TRUE(
        (darbotdb::auth::Level::RW ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
  }

  // test auth wildcard (revoke)
  {
    auto collectionJson = darbotdb::velocypack::Parser::fromJson(
        "{ \"name\": \"testDataSource\" }");
    darbotdb::auth::UserMap userMap;
    darbotdb::auth::User* userPtr = nullptr;
    userManager->setAuthInfo(userMap);  // insure an empty map is set before
                                        // UserManager::storeUser(...)
    userManager->storeUser(false, userName, darbotdb::StaticStrings::Empty,
                           true, darbotdb::velocypack::Slice());
    userManager->accessUser(
        userName,
        [&userPtr](darbotdb::auth::User const& user) -> darbotdb::Result {
          userPtr = const_cast<darbotdb::auth::User*>(&user);
          return darbotdb::Result();
        });
    ASSERT_NE(nullptr, userPtr);
    userPtr->grantCollection(
        vocbase->name(), "testDataSource",
        darbotdb::auth::Level::RO);  // for missing collections
                                     // User::collectionAuthLevel(...) returns
                                     // database auth::Level
    auto logicalCollection = std::shared_ptr<darbotdb::LogicalCollection>(
        vocbase->createCollection(collectionJson->slice()).get(),
        [vocbase](darbotdb::LogicalCollection* ptr) -> void {
          vocbase->dropCollection(ptr->id(), false);
        });
    ASSERT_FALSE(!logicalCollection);

    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(vocbase->name(), "testDataSource")));
    auto status = revokeWildcardHandler.execute();
    EXPECT_EQ(darbotdb::RestStatus::DONE, status);
    EXPECT_TRUE((darbotdb::rest::ResponseCode::ACCEPTED ==
                 revokeWildcardResponce.responseCode()));
    auto slice = revokeWildcardResponce._payload.slice();
    EXPECT_TRUE(slice.isObject());
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Code) &&
         slice.get(darbotdb::StaticStrings::Code).isNumber<size_t>() &&
         size_t(darbotdb::rest::ResponseCode::ACCEPTED) ==
             slice.get(darbotdb::StaticStrings::Code).getNumber<size_t>()));
    EXPECT_TRUE(
        (slice.hasKey(darbotdb::StaticStrings::Error) &&
         slice.get(darbotdb::StaticStrings::Error).isBoolean() &&
         false == slice.get(darbotdb::StaticStrings::Error).getBoolean()));
    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(
             vocbase->name(),
             "testDataSource")));  // unchanged since revocation is only for
                                   // exactly matching collection names
  }
}
