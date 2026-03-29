'use strict';
const db = require('@darbotdb').db;
const name = module.context.collectionName('setup_teardown');

if (!db._collection(name)) {
  db._create(name);
}

