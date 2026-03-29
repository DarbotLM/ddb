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

#ifndef USE_V8
#error this file is not supposed to be used in builds with -DUSE_V8=Off
#endif

#include "Mocks/Servers.h"  // this must be first because windows

#include <v8.h>

#include "gtest/gtest.h"

#include "velocypack/Builder.h"

#include "IResearch/common.h"
#include "Mocks/LogLevels.h"

#include "ApplicationFeatures/HttpEndpointProvider.h"
#include "Aql/QueryRegistry.h"
#include "Auth/UserManagerMock.h"
#include "Basics/StaticStrings.h"
#include "GeneralServer/AuthenticationFeature.h"
#include "Replication/ReplicationFeature.h"
#include "RestServer/DatabaseFeature.h"
#include "RestServer/SystemDatabaseFeature.h"
#include "RestServer/ViewTypesFeature.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "Utils/ExecContext.h"
#include "V8/v8-vpack.h"
#include "V8Server/V8DealerFeature.h"
#include "V8/V8SecurityFeature.h"
#include "V8Server/v8-users.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/LogicalView.h"
#include "VocBase/vocbase.h"

#if USE_ENTERPRISE
#include "Enterprise/Encryption/EncryptionFeature.h"
#endif

namespace {
struct TestView : darbotdb::LogicalView {
  darbotdb::Result _appendVelocyPackResult;
  darbotdb::velocypack::Builder _properties;

  static constexpr auto typeInfo() noexcept {
    return std::pair{static_cast<darbotdb::ViewType>(42),
                     std::string_view{"testViewType"}};
  }

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

class V8UsersTest
    : public ::testing::Test,
      public darbotdb::tests::LogSuppressor<darbotdb::Logger::AUTHENTICATION,
                                            darbotdb::LogLevel::ERR> {
 protected:
  darbotdb::tests::mocks::MockAqlServer server;
  ViewFactory viewFactory;
  darbotdb::SystemDatabaseFeature::ptr system;

  V8UsersTest()
      : server(),
        system(server.getFeature<darbotdb::SystemDatabaseFeature>().use()) {
    darbotdb::tests::v8Init();  // on-time initialize V8
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

TEST_F(V8UsersTest, test_collection_auth) {
  static const std::string userName("testUser");
  auto& databaseFeature = server.getFeature<darbotdb::DatabaseFeature>();
  TRI_vocbase_t* vocbase;  // will be owned by DatabaseFeature
  ASSERT_TRUE(
      databaseFeature.createDatabase(testDBInfo(server.server()), vocbase)
          .ok());
  v8::Isolate::CreateParams isolateParams;
  auto arrayBufferAllocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  isolateParams.array_buffer_allocator = arrayBufferAllocator.get();
  auto isolate = std::shared_ptr<v8::Isolate>(
      v8::Isolate::New(isolateParams),
      [](v8::Isolate* p) -> void { p->Dispose(); });
  ASSERT_NE(nullptr, isolate);

  // otherwise v8::Isolate::Logger() will fail (called from
  // v8::Exception::Error)
  v8::Isolate::Scope isolateScope(isolate.get());
  // otherwise v8::Isolate::Logger() will fail (called from
  // v8::Exception::Error)
  // required for v8::Context::New(...), v8::ObjectTemplate::New(...) and
  // TRI_AddMethodVocbase(...)
  v8::HandleScope handleScope(isolate.get());
  auto context = v8::Context::New(isolate.get());
  // required for TRI_AddMethodVocbase(...)
  v8::Context::Scope contextScope(context);
  // create and set inside 'isolate' for use with 'TRI_GET_GLOBALS()'
  std::unique_ptr<V8Global<darbotdb::ArangodServer>> v8g(
      CreateV8Globals(server.server(), isolate.get(), 0));
  // otherwise v8:-utils::CreateErrorObject(...) will fail
  v8g->ArangoErrorTempl.Reset(isolate.get(),
                              v8::ObjectTemplate::New(isolate.get()));
  v8g->_vocbase = vocbase;
  TRI_InitV8Users(context, vocbase, v8g.get(), isolate.get());
  auto arangoUsers =
      v8::Local<v8::ObjectTemplate>::New(isolate.get(), v8g->UsersTempl)
          ->NewInstance(TRI_IGETC)
          .FromMaybe(v8::Local<v8::Object>());
  auto fn_grantCollection =
      arangoUsers
          ->Get(context, TRI_V8_ASCII_STRING(isolate.get(), "grantCollection"))
          .FromMaybe(v8::Local<v8::Value>());
  EXPECT_TRUE(fn_grantCollection->IsFunction());
  auto fn_revokeCollection =
      arangoUsers
          ->Get(context, TRI_V8_ASCII_STRING(isolate.get(), "revokeCollection"))
          .FromMaybe(v8::Local<v8::Value>());
  EXPECT_TRUE(fn_revokeCollection->IsFunction());
  std::vector<v8::Local<v8::Value>> grantArgs = {
      TRI_V8_STD_STRING(isolate.get(), userName),
      TRI_V8_STD_STRING(isolate.get(), vocbase->name()),
      TRI_V8_ASCII_STRING(isolate.get(), "testDataSource"),
      TRI_V8_STD_STRING(isolate.get(), darbotdb::auth::convertFromAuthLevel(
                                           darbotdb::auth::Level::RW)),
  };
  std::vector<v8::Local<v8::Value>> grantWildcardArgs = {
      TRI_V8_STD_STRING(isolate.get(), userName),
      TRI_V8_STD_STRING(isolate.get(), vocbase->name()),
      TRI_V8_ASCII_STRING(isolate.get(), "*"),
      TRI_V8_STD_STRING(isolate.get(), darbotdb::auth::convertFromAuthLevel(
                                           darbotdb::auth::Level::RW)),
  };
  std::vector<v8::Local<v8::Value>> revokeArgs = {
      TRI_V8_STD_STRING(isolate.get(), userName),
      TRI_V8_STD_STRING(isolate.get(), vocbase->name()),
      TRI_V8_ASCII_STRING(isolate.get(), "testDataSource"),
  };
  std::vector<v8::Local<v8::Value>> revokeWildcardArgs = {
      TRI_V8_STD_STRING(isolate.get(), userName),
      TRI_V8_STD_STRING(isolate.get(), vocbase->name()),
      TRI_V8_ASCII_STRING(isolate.get(), "*"),
  };

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
  auto* authFeature = darbotdb::AuthenticationFeature::instance();
  auto* userManager = authFeature->userManager();

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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result = v8::Function::Cast(*fn_grantCollection)
                      ->CallAsFunction(context, arangoUsers,
                                       static_cast<int>(grantArgs.size()),
                                       grantArgs.data());
    EXPECT_TRUE(result.IsEmpty());
    EXPECT_TRUE(tryCatch.HasCaught());
    TRI_V8ToVPack(isolate.get(), response, tryCatch.Exception(), false);
    auto slice = response.slice();
    EXPECT_TRUE(slice.isObject());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result = v8::Function::Cast(*fn_revokeCollection)
                      ->CallAsFunction(context, arangoUsers,
                                       static_cast<int>(revokeArgs.size()),
                                       revokeArgs.data());
    EXPECT_TRUE(result.IsEmpty());
    EXPECT_TRUE(tryCatch.HasCaught());
    TRI_V8ToVPack(isolate.get(), response, tryCatch.Exception(), false);
    auto slice = response.slice();
    EXPECT_TRUE(slice.isObject());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result = v8::Function::Cast(*fn_grantCollection)
                      ->CallAsFunction(context, arangoUsers,
                                       static_cast<int>(grantArgs.size()),
                                       grantArgs.data());
    ASSERT_FALSE(result.IsEmpty());
    EXPECT_TRUE(result.ToLocalChecked()->IsUndefined());
    EXPECT_FALSE(tryCatch.HasCaught());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result = v8::Function::Cast(*fn_revokeCollection)
                      ->CallAsFunction(context, arangoUsers,
                                       static_cast<int>(revokeArgs.size()),
                                       revokeArgs.data());
    EXPECT_FALSE(result.IsEmpty());
    EXPECT_TRUE(result.ToLocalChecked()->IsUndefined());
    EXPECT_FALSE(tryCatch.HasCaught());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result = v8::Function::Cast(*fn_grantCollection)
                      ->CallAsFunction(context, arangoUsers,
                                       static_cast<int>(grantArgs.size()),
                                       grantArgs.data());
    EXPECT_TRUE(result.IsEmpty());
    EXPECT_TRUE(tryCatch.HasCaught());
    TRI_V8ToVPack(isolate.get(), response, tryCatch.Exception(), false);
    auto slice = response.slice();
    EXPECT_TRUE(slice.isObject());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result = v8::Function::Cast(*fn_revokeCollection)
                      ->CallAsFunction(context, arangoUsers,
                                       static_cast<int>(revokeArgs.size()),
                                       revokeArgs.data());
    EXPECT_TRUE(result.IsEmpty());
    EXPECT_TRUE(tryCatch.HasCaught());
    TRI_V8ToVPack(isolate.get(), response, tryCatch.Exception(), false);
    auto slice = response.slice();
    EXPECT_TRUE(slice.isObject());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result =
        v8::Function::Cast(*fn_grantCollection)
            ->CallAsFunction(context, arangoUsers,
                             static_cast<int>(grantWildcardArgs.size()),
                             grantWildcardArgs.data());
    EXPECT_FALSE(result.IsEmpty());
    EXPECT_TRUE(result.ToLocalChecked()->IsUndefined());
    EXPECT_FALSE(tryCatch.HasCaught());
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
    darbotdb::velocypack::Builder response;
    v8::TryCatch tryCatch(isolate.get());
    auto result =
        v8::Function::Cast(*fn_revokeCollection)
            ->CallAsFunction(context, arangoUsers,
                             static_cast<int>(revokeWildcardArgs.size()),
                             revokeWildcardArgs.data());
    EXPECT_FALSE(result.IsEmpty());
    EXPECT_TRUE(result.ToLocalChecked()->IsUndefined());
    EXPECT_FALSE(tryCatch.HasCaught());
    EXPECT_TRUE(
        (darbotdb::auth::Level::RO ==
         execContext->collectionAuthLevel(
             vocbase->name(),
             "testDataSource")));  // unchanged since revocation is only for
                                   // exactly matching collection names
  }
}
