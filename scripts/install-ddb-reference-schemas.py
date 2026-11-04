#!/usr/bin/env python3
"""Install DDB reference schemas and optional SQLite .ddb seed files."""

from __future__ import annotations

import argparse
import json
import shutil
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "ddb" / "schemas"
DEFAULT_TARGET = Path.home() / ".ddb" / "schemas"
DEFAULT_MICRO_DB = Path.home() / ".ddb" / "reference" / "ddb_agent_memory.ddb"


def copy_schemas(target: Path) -> None:
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(SCHEMA_DIR, target)


def create_reference_microdb(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(path) as conn:
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS cards (
              id TEXT PRIMARY KEY,
              title TEXT NOT NULL,
              card_type TEXT NOT NULL,
              zone TEXT,
              agent_id TEXT,
              content_markdown TEXT,
              card_json TEXT NOT NULL,
              semantic_attributes_json TEXT,
              relatable_entities_json TEXT,
              provenance_json TEXT,
              lineage_json TEXT,
              confidence REAL,
              sensitivity_label TEXT,
              hash TEXT,
              created_at TEXT DEFAULT CURRENT_TIMESTAMP,
              updated_at TEXT DEFAULT CURRENT_TIMESTAMP
            );

            CREATE VIRTUAL TABLE IF NOT EXISTS cards_fts
            USING fts5(title, content_markdown, zone, agent_id, content='cards', content_rowid='rowid');

            CREATE TABLE IF NOT EXISTS triples (
              id TEXT PRIMARY KEY,
              subject TEXT NOT NULL,
              predicate TEXT NOT NULL,
              object TEXT NOT NULL,
              relationship_type TEXT,
              confidence REAL,
              provenance_json TEXT,
              lineage_json TEXT,
              semantics_json TEXT,
              valid_from TEXT,
              valid_to TEXT,
              created_at TEXT DEFAULT CURRENT_TIMESTAMP
            );

            CREATE INDEX IF NOT EXISTS triples_subject_predicate ON triples(subject, predicate);
            CREATE INDEX IF NOT EXISTS triples_object_predicate ON triples(object, predicate);

            CREATE TABLE IF NOT EXISTS embeddings (
              id TEXT PRIMARY KEY,
              owner_id TEXT NOT NULL,
              owner_kind TEXT NOT NULL,
              model TEXT,
              vector_ref TEXT,
              dimension INTEGER,
              distance TEXT,
              metadata_json TEXT,
              created_at TEXT DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS metadata (
              key TEXT PRIMARY KEY,
              value TEXT NOT NULL,
              updated_at TEXT DEFAULT CURRENT_TIMESTAMP
            );
            """
        )
        registry = json.loads((SCHEMA_DIR / "schema_registry.json").read_text(encoding="utf-8"))
        conn.execute(
            "INSERT OR REPLACE INTO metadata(key, value) VALUES (?, ?)",
            ("schema_registry", json.dumps(registry, indent=2)),
        )
        conn.execute(
            "INSERT OR REPLACE INTO metadata(key, value) VALUES (?, ?)",
            ("semantic_alignment", "complete"),
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Install DDB semantic alignment schemas and reference DDBs.")
    parser.add_argument("--target", type=Path, default=DEFAULT_TARGET, help="Directory where schemas are installed.")
    parser.add_argument(
        "--create-reference-microdb",
        action="store_true",
        help="Create a deployable SQLite .ddb reference micro database.",
    )
    parser.add_argument("--microdb-path", type=Path, default=DEFAULT_MICRO_DB, help="Path for the reference .ddb file.")
    args = parser.parse_args()

    copy_schemas(args.target)
    print(f"Installed DDB schemas to {args.target}")

    if args.create_reference_microdb:
        create_reference_microdb(args.microdb_path)
        print(f"Created reference SQLite .ddb at {args.microdb_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

