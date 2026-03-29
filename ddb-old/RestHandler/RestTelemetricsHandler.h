////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ddbB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/ddbb/ddbb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ddbB GmbH, Cologne, Germany
///
/// @author Julia Puget
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RestHandler/RestBaseHandler.h"

namespace ddbb {
namespace velocypack {
class Builder;
}

class RestTelemetricsHandler : public ddbb::RestBaseHandler {
 public:
  RestTelemetricsHandler(ddbServer&, GeneralRequest*, GeneralResponse*);

  char const* name() const override final { return "RestTelemetricsHandler"; }
  RequestLane lane() const override final { return RequestLane::CLIENT_SLOW; }
  RestStatus execute() override;

 private:
  void resetTelemetricsRequestsCounter();
  bool trackTelemetricsRequestsCounter();
};
}  // namespace ddbb
