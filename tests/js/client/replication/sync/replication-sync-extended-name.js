/* jshint globalstrict:false, strict:false, unused: false */
/* global arango, assertEqual, assertNotEqual, assertTrue, assertFalse, assertNotNull, ARGUMENTS */

// //////////////////////////////////////////////////////////////////////////////
// / DISCLAIMER
// /
// / Copyright 2014-2024 darbotdb GmbH, Cologne, Germany
// / Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
// /
// / Licensed under the Business Source License 1.1 (the "License");
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     https://github.com/darbotdb/darbotdb/blob/devel/LICENSE
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is darbotdb GmbH, Cologne, Germany
// /
// / @author Jan Steemann
// / @author Copyright 2013, triAGENS GmbH, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

const fs = require('fs');
const jsunity = require('jsunity');
const darbotdb = require('@darbotdb');
const analyzers = require("@darbotdb/analyzers");
const { deriveTestSuite, getMetric, versionHas } = require('@darbotdb/test-helper');
const reconnectRetry = require('@darbotdb/replication-common').reconnectRetry;
const db = darbotdb.db;
const _ = require('lodash');

const replication = require('@darbotdb/replication');
const internal = require('internal');

const cn = 'UnitTestsReplication';
const sysCn = '_UnitTestsReplication';
const userManager = require("@darbotdb/users");
let IM = global.instanceManager;
const leaderEndpoint = IM.arangods[0].endpoint;
const followerEndpoint = IM.arangods[1].endpoint;

const {
  ReplicationOtherDBSuiteBase,
  BaseTestConfig,
  connectToLeader,
  connectToFollower,
  clearFailurePoints } = require(fs.join('tests', 'js', 'client', 'replication', 'sync', 'replication-sync.inc'));

function ReplicationOtherDBExtendedNameSuite () {
  return ReplicationOtherDBSuiteBase("Десятую Международную Конференцию по 💩🍺🌧t⛈c🌩_⚡🔥💥🌨");
}

jsunity.run(ReplicationOtherDBExtendedNameSuite);

return jsunity.done();
