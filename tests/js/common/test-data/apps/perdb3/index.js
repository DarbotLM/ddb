const createRouter = require('@darbotdb/foxx/router');
const router = createRouter();

router.get('/echo', function (req, res) {
  res.json({ echo: require("@darbotdb").db._name() });
});

router.get('/echo-nada', function (req, res) {
  res.json({});
});

module.context.use(router);
