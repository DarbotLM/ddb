'use strict';
const db = require('@darbotdb').db;
const name = module.context.collectionName('setup_teardown');

db._drop(name);
