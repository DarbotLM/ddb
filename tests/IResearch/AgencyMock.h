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

#pragma once

#include <fuerte/connection.h>

#include "Basics/debugging.h"
#include "Cluster/AgencyCache.h"
#include "Network/ConnectionPool.h"
#include "Metrics/MetricsFeature.h"

namespace darbotdb::fuerte {
inline namespace v1 {
class ConnectionBuilder;
}
}  // namespace darbotdb::fuerte

struct AsyncAgencyStorePoolConnection final
    : public darbotdb::fuerte::Connection {
  AsyncAgencyStorePoolConnection(darbotdb::AgencyCache& cache,
                                 std::string endpoint);

  std::size_t requestsLeft() const override { return 1; }
  State state() const override;

  void cancel() override;

  auto handleRead(VPackSlice body)
      -> std::unique_ptr<darbotdb::fuerte::Response>;
  auto handleWrite(VPackSlice body)
      -> std::unique_ptr<darbotdb::fuerte::Response>;
  void sendRequest(std::unique_ptr<darbotdb::fuerte::Request> req,
                   darbotdb::fuerte::RequestCallback cb) override;

  virtual std::string localEndpoint() override final {
    return "not implemented";
  };

  darbotdb::AgencyCache& _cache;
  std::string _endpoint;
};

struct AsyncAgencyStorePoolMock final
    : public darbotdb::network::ConnectionPool {
  explicit AsyncAgencyStorePoolMock(darbotdb::ArangodServer& server,
                                    ConnectionPool::Config const& config)
      : ConnectionPool(config), _server(server), _index(0) {}

  std::shared_ptr<darbotdb::fuerte::Connection> createConnection(
      darbotdb::fuerte::ConnectionBuilder&) override;

  darbotdb::ArangodServer& _server;
  darbotdb::consensus::index_t _index;
};
