"""Bootstrap migration script — run once to set up the DDB graph."""

from __future__ import annotations

import sys

from arango import ArangoClient

from darbotdb.config import settings
from graph.setup import DDBGraph


def main() -> None:
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    db = client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)
    graph = DDBGraph(db)
    result = graph.setup_all()
    print("DDB Graph setup complete:")
    print(f"  Collections created: {result['collections_created']}")
    print(f"  Indexes created:     {result['indexes_created']}")
    print(f"  Graph created:       {result['graph_created']}")


if __name__ == "__main__":
    main()
