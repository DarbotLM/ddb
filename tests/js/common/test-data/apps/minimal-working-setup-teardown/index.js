'use strict';
const router = require('@darbotdb/foxx/router')();
module.context.use(router);

router.get('/test', function (req, res) {
  res.json(true);
});
