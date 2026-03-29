"""SessionFactory -- create DBSession instances by backend name."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from session.protocol import DBSession


class SessionFactory:
    """Create DBSession instances by backend type.

    Usage::

        session = await SessionFactory.create("micro", db_path="data/agents/bot.ddb")
        session = await SessionFactory.create("arango", hosts="http://localhost:8529", ...)
        session = await SessionFactory.create("hybrid", db_path="...", hosts="...", ...)
    """

    @staticmethod
    async def create(backend: str, **config: Any) -> DBSession:
        """Instantiate and return a DBSession.

        Args:
            backend: One of "micro" | "arango" | "hybrid".
            **config: Backend-specific keyword arguments.

              For "micro":
                db_path (str | Path): Path to the .ddb file.

              For "arango":
                hosts (str): ArangoDB hosts URL.
                database (str): Database name.
                username (str): Username.
                password (str): Password.

              For "hybrid":
                All of the above combined.
        """
        if backend == "micro":
            return await SessionFactory._make_micro(**config)
        if backend == "arango":
            return SessionFactory._make_arango(**config)
        if backend == "hybrid":
            return await SessionFactory._make_hybrid(**config)
        raise ValueError(f"Unknown backend: {backend!r}. Choose micro | arango | hybrid.")

    @staticmethod
    async def _make_micro(**config: Any):
        from micro.engine import MicroDB
        from session.micro_session import MicroDBSession
        db_path = Path(config.get("db_path", "agent.ddb"))
        db = MicroDB(db_path)
        if db_path.exists():
            await db.open()
        else:
            await db.create(**{k: v for k, v in config.items() if k != "db_path"})
        return MicroDBSession(db)

    @staticmethod
    def _make_arango(**config: Any):
        from arango import ArangoClient
        from session.arango_session import ArangoDBSession
        client = ArangoClient(hosts=config.get("hosts", "http://localhost:8529"))
        db = client.db(
            config.get("database", "txt2kg"),
            username=config.get("username", "root"),
            password=config.get("password", ""),
        )
        return ArangoDBSession(db)

    @staticmethod
    async def _make_hybrid(**config: Any):
        from session.hybrid_session import HybridSession
        micro = await SessionFactory._make_micro(**config)
        arango = SessionFactory._make_arango(**config)
        return HybridSession(micro, arango)