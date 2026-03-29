'use strict';

const db = require('@darbotdb').db;
db['foxx_queue_test'].save({job: true});
