'use strict';

const db = require('@darbotdb').db;
const name = 'foxx_queue_test';

if (db._collection(name)) {
  db._drop(name);
}

const queues = require('@darbotdb/foxx/queues');
queues.delete('test_123');
