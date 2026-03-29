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
/// @author Manuel Pöter
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <string_view>

#include "VocBase/voc-types.h"

#include <velocypack/Slice.h>

#include "Options.h"
#include "Report.h"

namespace darbotdb {
class LogicalCollection;
}

namespace darbotdb::sepp {

struct Server;

class Runner {
 public:
  Runner(std::string_view executable, std::string_view reportFile,
         velocypack::Slice config);
  ~Runner();
  void run();

 private:
  void startServer();
  void setup();
  auto createCollection(std::string const& name, std::string const& type)
      -> std::shared_ptr<LogicalCollection>;
  void createIndex(LogicalCollection& col, IndexSetup const& index);
  auto runBenchmark() -> Report;
  void printSummary(Report const& report);
  void writeReport(Report const& report);

  std::string_view _executable;
  std::string _reportFile;

  Options _options;
  std::unique_ptr<Server> _server;
};

}  // namespace darbotdb::sepp
