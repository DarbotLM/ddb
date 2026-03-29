"""DDB Session abstraction layer.

Provides a unified read/write interface over SQLite micro DBs and ArangoDB.

Usage::

    from session import SessionFactory, DBSession

    session = await SessionFactory.create("micro", db_path="agent.ddb")
    session = await SessionFactory.create("hybrid", db_path="agent.ddb",
                                          hosts="http://localhost:8529",
                                          database="txt2kg")
"""

from session.protocol import DBSession
from session.models import NodeRecord, EdgeRecord, TripleRecord
from session.micro_session import MicroDBSession
from session.arango_session import ArangoDBSession
from session.hybrid_session import HybridSession
from session.factory import SessionFactory

__all__ = [
    "DBSession",
    "NodeRecord",
    "EdgeRecord",
    "TripleRecord",
    "MicroDBSession",
    "ArangoDBSession",
    "HybridSession",
    "SessionFactory",
]