"""First-party DDB schema resources."""

from __future__ import annotations

from importlib.resources import files
from pathlib import Path

KG_DV_SCHEMA = "kg_dv_schema.json"
KG_DV_SCHEMA_CONTRACT = "kg_dv_schema.schema.json"
AC3_ADAPTIVE_AGENT_CARD_SCHEMA = "ac3_adaptive_agent_card.schema.json"
AC3_ADAPTIVE_AGENT_CARD_EXAMPLE = "ac3_adaptive_agent_card.example.yaml"
SCHEMA_REGISTRY = "schema_registry.json"
REFERENCE_DDB_KG = "reference_ddbs/ddb_reference_kg.json"
REFERENCE_DDB_AGENT_MEMORY = "reference_ddbs/ddb_agent_memory.json"
REFERENCE_DDB_INTEGRATION_INDEX = "reference_ddbs/ddb_integration_index.json"


def schema_path(name: str) -> Path:
    """Return the filesystem path for a packaged schema resource."""
    return Path(str(files(__package__) / name))

