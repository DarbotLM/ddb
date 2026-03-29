const createRouter = require('@darbotdb/foxx/router');
const router = createRouter();

router.get('/execute', function (req, res) {
  let db = require("@darbotdb").db;
  db._executeTransaction({
    collections: { write: "UnitTestsTransaction" },
    action: function() {
      let db = require("@darbotdb").db;
      db.UnitTestsTransaction.insert({});
    }
  });
  res.json({ count: db.UnitTestsTransaction.count() });
});

module.context.use(router);
