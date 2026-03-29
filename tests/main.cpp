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
/// @author Andreas Streichardt
////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <thread>

#include "gtest/gtest.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "ApplicationFeatures/ShellColorsFeature.h"
#include "Basics/ArangoGlobalContext.h"
#include "Basics/ConditionVariable.h"
#include "Basics/Thread.h"
#include "Basics/icu-helper.h"
#include "Cluster/ServerState.h"
#include "ClusterEngine/ClusterEngine.h"
#include "Logger/Logger.h"
#include "Random/RandomGenerator.h"
#include "Rest/Version.h"
#include "RestServer/ServerIdFeature.h"
#include "VocBase/Identifiers/ServerId.h"

template<class Function>
class TestThread : public darbotdb::Thread {
 public:
  TestThread(darbotdb::ArangodServer& server, Function&& f, int i, char* c[])
      : darbotdb::Thread(server, "gtest"), _f(f), _i(i), _c(c), _done(false) {
    run();
    std::unique_lock guard{_wait.mutex};
    while (true) {
      if (_done) {
        break;
      }
      _wait.cv.wait_for(guard, std::chrono::seconds{1});
    }
  }
  ~TestThread() { shutdown(); }

  void run() override {
    std::lock_guard guard{_wait.mutex};
    _result = _f(_i, _c);
    _done = true;
    _wait.cv.notify_all();
  }

  int result() { return _result; }

 private:
  Function _f;
  int _i;
  char** _c;
  std::atomic<bool> _done;
  std::atomic<int> _result;
  darbotdb::basics::ConditionVariable _wait;
};

char const* ARGV0 = "";

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  // our gtest version is old and doesn't have the GTEST_FLAG_SET macro yet,
  // thus this funny workaround for now.
  // GTEST_FLAG_SET(death_test_style, "threadsafe");
  (void)(::testing::GTEST_FLAG(death_test_style) = "threadsafe");

  TRI_GET_ARGV(argc, argv);
  int subargc = 0;
  char** subargv = (char**)malloc(sizeof(char*) * argc);
  bool logLineNumbers = false;
  darbotdb::RandomGenerator::initialize(
      darbotdb::RandomGenerator::RandomType::MERSENNE);
  // global setup...
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0) {
      darbotdb::rest::Version::initialize();
      std::cout << darbotdb::rest::Version::getServerVersion() << std::endl
                << std::endl
                << darbotdb::rest::Version::getDetailed() << std::endl;
      exit(EXIT_SUCCESS);
    }
    if (strcmp(argv[i], "--log.line-number") == 0) {
      if (i < argc) {
        i++;
        if (i < argc) {
          if (strcmp(argv[i], "true") == 0) {
            logLineNumbers = true;
          }
          i++;
        }
      }
    } else {
      subargv[subargc] = argv[i];
      subargc++;
    }
  }

  ARGV0 = subargv[0];

  darbotdb::ArangodServer server(nullptr, nullptr);
  darbotdb::ServerState state(server);
  state.setRole(darbotdb::ServerState::ROLE_SINGLE);
  darbotdb::ShellColorsFeature sc(server);

  darbotdb::Logger::setShowLineNumber(logLineNumbers);
  darbotdb::Logger::setTimeFormat(
      darbotdb::LogTimeFormats::TimeFormat::UTCDateStringMicros);
  darbotdb::Logger::setShowThreadIdentifier(true);
  darbotdb::Logger::initialize(false, 10000);
  darbotdb::Logger::addAppender(darbotdb::Logger::defaultLogGroup(), "-");

  sc.prepare();

  darbotdb::ArangoGlobalContext ctx(1, const_cast<char**>(&ARGV0), ".");
  ctx.exit(0);  // set "good" exit code by default

  darbotdb::ServerIdFeature::setId(darbotdb::ServerId{12345});
  // many other places rely on the reboot id being initialized,
  // so we do it here in a central place
  darbotdb::ServerState::instance()->setRebootId(darbotdb::RebootId{1});
  darbotdb::ServerState::instance()->setGoogleTest(true);
  IcuInitializer::setup(ARGV0);

  // enable mocking globally - not awesome, but helps to prevent runtime
  // assertions in queries
  darbotdb::ClusterEngine::Mocking = true;

  // Run tests in subthread such that it has a larger stack size in libmusl,
  // the stack size for subthreads has been reconfigured in the
  // ArangoGlobalContext above in the libmusl case:
  int result;
  auto tests = [](int argc, char* argv[]) -> int { return RUN_ALL_TESTS(); };
  TestThread<decltype(tests)> t(server, std::move(tests), subargc, subargv);
  result = t.result();

  darbotdb::Logger::shutdown();
  // global clean-up...
  free(subargv);
  return (result < 0xff ? result : 0xff);
}
