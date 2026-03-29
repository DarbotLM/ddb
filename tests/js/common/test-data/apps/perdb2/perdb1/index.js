
const createRouter = require('@darbotdb/foxx/router');
const router = createRouter();

router.get('/echo', function (req, res) {
  res.json({ db: require("@darbotdb").db._name() });
});

module.context.use(router);
