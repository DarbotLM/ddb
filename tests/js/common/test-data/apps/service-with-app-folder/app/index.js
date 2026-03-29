'use strict';
const router = require('@darbotdb/foxx/router')();
module.context.use(router);
router.get((req, res) => {
  res.send({hello: 'world'});
});

