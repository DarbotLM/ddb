"""MicroDBManager -- lifecycle management for .ddb micro databases."""

from __future__ import annotations

from pathlib import Path

from micro.engine import MicroDB


class MicroDBManager:
    """Create, open, list, and delete .ddb micro databases."""

    DIRS = [
        "agents",
        "sessions",
        "sessions/shared",
        "memory",
        "index",
    ]

    def __init__(self, base_path: Path) -> None:
        base = Path(base_path)
        self.base = base if base.name.lower() == "data" else base / "data"
        self._ensure_dirs()

    def _ensure_dirs(self) -> None:
        for d in self.DIRS:
            (self.base / d).mkdir(parents=True, exist_ok=True)

    async def create_agent_db(self, agent_id: str, **meta: str) -> MicroDB:
        path = self.base / "agents" / f"{agent_id}.ddb"
        db = MicroDB(path)
        await db.create(agent_id=agent_id, db_type="agent", **meta)
        return db

    async def create_session_db(self, session_id: str, agent_id: str) -> MicroDB:
        path = self.base / "sessions" / f"{session_id}.ddb"
        db = MicroDB(path)
        await db.create(session_id=session_id, agent_id=agent_id, db_type="session")
        return db

    async def create_zone_db(self, zone_name: str) -> MicroDB:
        path = self.base / "sessions" / "shared" / f"{zone_name}.ddb"
        db = MicroDB(path)
        await db.create(zone=zone_name, db_type="zone", visibility="shared")
        return db

    async def create_memory_db(self, agent_id: str) -> MicroDB:
        path = self.base / "memory" / f"{agent_id}_memory.ddb"
        db = MicroDB(path)
        await db.create(agent_id=agent_id, db_type="memory")
        return db

    async def open_db(self, db_path: str | Path) -> MicroDB:
        path = Path(db_path)
        if not path.is_absolute():
            path = self.base / path
        db = MicroDB(path)
        await db.open()
        return db

    def list_dbs(self) -> list[Path]:
        return sorted(self.base.rglob("*.ddb"))

    async def delete_db(self, db_path: str | Path) -> None:
        path = Path(db_path)
        if not path.is_absolute():
            path = self.base / path
        for suffix in ("", "-journal", "-wal", "-shm"):
            target = path.parent / (path.name + suffix)
            if target.exists():
                target.unlink()